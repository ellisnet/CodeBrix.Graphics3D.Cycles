// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Phase E3: scene edits after a render.
//
// The pinned static-BVH update path (ObjectManager::apply_static_transforms)
// bakes a single-user geometry with its object transform: it rewrites the
// geometry's position/normal/radius attributes in place and sets
// Geometry::transform_applied. Cycles has no inverse of that step; its
// interactive hosts rebuild the geometry from their own data. This host keeps
// the authored attributes of every geometry the next render may bake. Before
// a geometry edit, and before a render whose fresh-scene bake would differ
// from the existing one (object transform or motion, motion blur, users,
// displacement/subsurface shaders), it restores those attributes and clears
// the baked state, so Cycles' own device update re-bakes or instances the
// geometry exactly as it does for a scene built from scratch. Volume
// geometry (voxel attributes) cannot be restored; such edits are refused.
// Included inside the private implementation namespace of cycles_host.cpp.
bool bake_restorable(const Geometry *geometry) {
  return geometry->is_mesh() || geometry->is_hair() || geometry->is_pointcloud();
}
// Copies every attribute (all motion steps) into `to`, whose geometry must
// have the same element counts as `from` was captured with.
void copy_attributes(const AttributeSet &from, AttributeSet &to) {
  for (const Attribute &attribute : from.attributes) {
    require(!(attribute.element & ATTR_ELEMENT_VOXEL), "Voxel attributes cannot be restored", CBC_FAILED);
    Attribute &copy = to.copy(attribute);
    copy.flags = attribute.flags;
    require(copy.size == attribute.size && copy.motion.size() == attribute.motion.size(), "Authored attribute layout changed", CBC_FAILED);
    const size_t bytes = size_t(attribute.size) * attribute.data_sizeof();
    for (int step = 0; step <= int(attribute.motion.size()); ++step)
      if (bytes) std::memcpy(copy.data_for_write(step), attribute.data(step), bytes);
  }
}
// What a scene built from scratch would do with this geometry at the next
// render: mirrors the apply conditions of ObjectManager::apply_static_transforms.
struct ExpectedBake { bool apply = false; Object *object = nullptr; Transform tfm; bool to_motion = false; };
ExpectedBake expected_bake(Scene &scene, Geometry *geometry) {
  ExpectedBake result;
  if (scene.params.bvh_type != BVH_TYPE_STATIC || geometry->is_light()) return result;
  Object *user = nullptr; int users = 0;
  for (Object *object : scene.objects) if (object->get_geometry() == geometry) { user = object; users++; }
  if (users != 1) return result;
  const Scene::MotionType motion = scene.need_motion();
  bool apply = !geometry->has_surface_bssrdf && !geometry->has_true_displacement();
  if (geometry->is_mesh()) apply = apply && static_cast<Mesh *>(geometry)->get_subdivision_type() == Mesh::SUBDIVISION_NONE;
  else if (geometry->is_hair() || geometry->is_pointcloud()) { float scale; apply = apply && transform_uniform_scale(user->get_tfm(), scale); }
  if (motion == Scene::MOTION_BLUR && user->use_motion()) apply = false;
  result.apply = apply; result.object = user; result.tfm = user->get_tfm();
  result.to_motion = motion != Scene::MOTION_PASS && motion != Scene::MOTION_PASS_INTERACTIVE;
  return result;
}
bool same_transform(const Transform &a, const Transform &b) { return std::memcmp(&a, &b, sizeof(Transform)) == 0; }
// A shader edit that may add true displacement or subsurface scattering
// changes Geometry::has_true_displacement/has_surface_bssrdf, which decide
// whether Cycles bakes the geometry.
bool shader_may_change_bake(Host &host, Node *node) {
  auto shader = static_cast<Shader *>(node);
  auto it = host.graphs.find(shader);
  if (it == host.graphs.end() || (!it->second->dirty && !host.edited_shaders.count(shader))) return false;
  auto &graph = *it->second->authoring;
  if (auto displacement = graph.output()->input("Displacement"); displacement && displacement->link) return true;
  for (auto node : graph.nodes) {
    const std::string name = node->type->name.string();
    if (name == "subsurface_scattering" || name == "principled_bsdf") return true;
  }
  return false;
}
// Restores the authored attributes of a baked geometry and tags a rebuild.
void unbake(Host &host, Geometry *geometry) {
  auto it = host.bakes.find(geometry);
  if (it == host.bakes.end()) return;
  if (geometry->transform_applied) {
    AttributeSet staged(geometry, geometry->attributes.prim);
    copy_attributes(*it->second.attributes, staged);
    geometry->attributes.update(std::move(staged));
    geometry->transform_applied = false; geometry->transform_negative_scaled = false;
    geometry->transform_normal = transform_identity();
    geometry->tag_modified(); geometry->tag_update(host.session->scene.get(), true);
  }
  host.bakes.erase(it);
}
const char *const volume_refusal = "Volume geometry was baked with its object transform by an earlier render and cannot be restored; "
                                   "delete the object and volume and create new ones to change its transform, motion, users or data";
// Called by every host function that edits a node, before the edit. A baked
// geometry gets its authored attributes back first, so the edit applies to
// authored data; edits that would need a baked volume restored are refused.
void before_edit(Host &host, Entry &entry, const char *socket_name) {
  if (entry.graph) return;
  if (auto geometry = dynamic_cast<Geometry *>(entry.node)) {
    if (geometry->transform_applied && geometry->is_volume()) throw Failure(CBC_NOT_READY, volume_refusal);
    if (geometry->transform_applied && bake_restorable(geometry)) unbake(host, geometry);
  } else if (auto object = dynamic_cast<Object *>(entry.node)) {
    const std::string name = socket_name ? socket_name : "";
    Geometry *geometry = object->get_geometry();
    if ((name == "tfm" || name == "motion" || name == "geometry") && geometry && geometry->is_volume() && geometry->transform_applied)
      throw Failure(CBC_NOT_READY, volume_refusal);
  }
}
// Before a render: restore every baked geometry whose fresh-scene bake would
// differ, then capture the authored attributes of geometry this render may bake.
void reconcile_bakes(Host &host) {
  Scene &scene = *host.session->scene;
  for (Geometry *geometry : scene.geometry) {
    if (!geometry->transform_applied || geometry->is_light()) continue;
    auto expected = expected_bake(scene, geometry);
    auto record = host.bakes.find(geometry);
    bool keep = record != host.bakes.end() && expected.apply && expected.object == record->second.object &&
                same_transform(expected.tfm, record->second.tfm) && expected.to_motion == record->second.to_motion;
    if (keep) for (Node *shader : geometry->get_used_shaders()) if (shader_may_change_bake(host, shader)) keep = false;
    if (keep) continue;
    if (geometry->is_volume()) throw Failure(CBC_NOT_READY, volume_refusal);
    // A baked geometry without a record predates this host's capture (for
    // example a shared geometry the device update baked); nothing to restore.
    if (record != host.bakes.end()) unbake(host, geometry);
  }
  for (Geometry *geometry : scene.geometry) {
    if (geometry->transform_applied || !bake_restorable(geometry)) continue;
    auto expected = expected_bake(scene, geometry);
    if (!expected.apply) { host.bakes.erase(geometry); continue; }
    BakeRecord record; record.object = expected.object; record.tfm = expected.tfm; record.to_motion = expected.to_motion;
    record.attributes = std::make_unique<AttributeSet>(geometry, geometry->attributes.prim);
    copy_attributes(geometry->attributes, *record.attributes);
    host.bakes[geometry] = std::move(record);
  }
  host.edited_shaders.clear();
}
// After a render (successful or not): drop captures Cycles did not bake.
void prune_bakes(Host &host) noexcept {
  for (auto it = host.bakes.begin(); it != host.bakes.end();) it = it->first->transform_applied ? std::next(it) : host.bakes.erase(it);
}
