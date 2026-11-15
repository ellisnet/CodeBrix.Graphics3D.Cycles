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
// Copies one attribute (all motion steps) into `to`, whose geometry must have
// the same element counts as the attribute was captured with.
void copy_attribute(const Attribute &attribute, AttributeSet &to) {
  require(!(attribute.element & ATTR_ELEMENT_VOXEL), "Voxel attributes cannot be restored", CBC_FAILED);
  Attribute &copy = to.copy(attribute);
  copy.flags = attribute.flags;
  require(copy.size == attribute.size && copy.motion.size() == attribute.motion.size(), "Authored attribute layout changed", CBC_FAILED);
  const size_t bytes = size_t(attribute.size) * attribute.data_sizeof();
  for (int step = 0; step <= int(attribute.motion.size()); ++step)
    if (bytes) std::memcpy(copy.data_for_write(step), attribute.data(step), bytes);
}
// Copies every attribute (all motion steps) into `to`.
void copy_attributes(const AttributeSet &from, AttributeSet &to) {
  for (const Attribute &attribute : from.attributes) copy_attribute(attribute, to);
}
// A point cloud that Cycles renders as Gaussian splats: render_as gaussian_splats with the standard radiance:base, scale
// and rotation attributes (PointCloud::primitive_type).
bool renders_splats(const Geometry *geometry) {
  return geometry->is_pointcloud() && (static_cast<const PointCloud *>(geometry)->primitive_type() & PRIMITIVE_GSPLAT);
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
  // Gaussian splats are never baked: their spherical harmonics would need rotating (the same rule, scene/object.cpp).
  if (renders_splats(geometry)) apply = false;
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
// Keeps the authored attributes of a geometry this render may bake, and the bake inputs.
void capture_bake(Host &host, Geometry *geometry, const ExpectedBake &expected) {
  BakeRecord record; record.object = expected.object; record.tfm = expected.tfm; record.to_motion = expected.to_motion;
  record.attributes = std::make_unique<AttributeSet>(geometry, geometry->attributes.prim);
  copy_attributes(geometry->attributes, *record.attributes);
  host.bakes[geometry] = std::move(record);
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
// Gaussian splats. Each device update of a modified splat cloud replaces its radius, centre and motion steps, with the
// bounding radius of each splat (PointCloud::update_gsplat_radii, from radiance:base and scale), and the BVH bounds the
// splats with that radius from then on. Before a render or bake the host keeps the authored radius of every splat cloud
// it does not hold yet; reads of radius answer from it (host_scene.inc, authored_attribute); and the next edit of the
// cloud writes it back first, so the edit applies to authored data and the next device update computes the bounding
// radius again, as for a scene built from scratch. Between renders the cloud keeps Cycles' radius, as Cycles' own scene
// does.
void keep_splat_radii(Host &host) {
  for (Geometry *geometry : host.session->scene->geometry) {
    if (!renders_splats(geometry) || host.splat_radii.count(geometry)) continue;
    auto authored = std::make_unique<AttributeSet>(geometry, geometry->attributes.prim);
    copy_attribute(*geometry->attributes.find(ATTR_STD_RADIUS), *authored);
    host.splat_radii[geometry] = std::move(authored);
  }
}
void restore_splat_radius(Host &host, Geometry *geometry) {
  auto it = host.splat_radii.find(geometry);
  if (it == host.splat_radii.end()) return;
  Attribute *radius = geometry->attributes.find(ATTR_STD_RADIUS);
  const Attribute *authored = it->second->find(ATTR_STD_RADIUS);
  require(radius && authored && radius->size == authored->size && radius->motion.size() == authored->motion.size(), "Authored radius layout changed", CBC_FAILED);
  const size_t bytes = size_t(authored->size) * authored->data_sizeof();
  for (int step = 0; step <= int(authored->motion.size()); ++step)
    if (bytes) std::memcpy(radius->data_for_write(step), authored->data(step), bytes);
  static_cast<PointCloud *>(geometry)->tag_radius_modified();
  geometry->tag_update(host.session->scene.get(), true);
  host.splat_radii.erase(it);
}
// True displacement. Whenever its device update finds a mesh modified, GeometryManager::displace (scene/mesh_displace.cpp)
// evaluates the displacement at the mesh's current vertices, adds it to the current positions (every motion step) and
// recomputes the normals from them; tangents and the undisplaced position and normal it adds only when missing. Cycles has
// no inverse of that step either, so an edit of a displaced mesh, a change of its displacement shader and the device
// recovery's complete update (which tags every geometry modified) would displace displaced data a second time. The host
// keeps the authored attributes of every triangle mesh a render displaces, from that render until the next edit of the
// mesh. An edit of the mesh restores them first and drops the copy (the edit applies to authored data, the next render
// captures it again); a render restores them before Cycles displaces the mesh again: when the mesh is modified (the device
// recovery included), when one of its displacement shaders changed (Cycles' own need_update_displacement, or recompiled
// for the render: an edited graph or a bound image updated), when the object the displacement is evaluated with changed
// (Cycles takes the first object that uses the mesh) or was edited (its transform, motion or any other socket the shader
// may read), or when the scene's motion type changed; in the last three cases Cycles would not displace again by itself,
// so the restored mesh is tagged modified. A mesh whose displacement is gone gets its authored attributes back and no copy.
// Subdivision meshes are left alone: tessellation rebuilds their triangles from the cage before each displacement.
bool displaces(const Geometry *geometry) {
  if (!geometry->is_mesh()) return false;
  auto mesh = static_cast<const Mesh *>(geometry);
  return mesh->get_num_subd_faces() == 0 && mesh->num_triangles() != 0 && mesh->has_true_displacement();
}
// The object Cycles evaluates a mesh's displacement with: the first that uses it (GeometryManager::displace).
Object *displacing_object(Scene &scene, const Geometry *geometry) {
  for (Object *object : scene.objects) if (object->get_geometry() == geometry) return object;
  return nullptr;
}
void restore_displaced(Host &host, Geometry *geometry, const DisplacedRecord &record) {
  AttributeSet staged(geometry, geometry->attributes.prim);
  copy_attributes(*record.attributes, staged);
  geometry->attributes.update(std::move(staged));
  geometry->tag_modified(); geometry->tag_update(host.session->scene.get(), true);
}
// Restores a displaced mesh's authored attributes and drops the copy.
void undisplace(Host &host, Geometry *geometry) {
  auto it = host.displaced.find(geometry);
  if (it == host.displaced.end()) return;
  restore_displaced(host, geometry, it->second);
  host.displaced.erase(it);
}
// Before a render or bake, after its shaders are compiled (prepare_graphs) and after the device recovery: restore every
// displaced mesh Cycles will displace again, and capture the authored attributes of every mesh this render may displace.
void reconcile_displacement(Host &host) {
  Scene &scene = *host.session->scene;
  const int motion = int(scene.need_motion());
  for (Geometry *geometry : scene.geometry) {
    if (!geometry->is_mesh()) continue;
    const bool displaced = displaces(geometry);
    Object *object = displacing_object(scene, geometry);
    auto it = host.displaced.find(geometry);
    if (it != host.displaced.end()) {
      if (!displaced) {
        // The displacement is gone: authored attributes, which the static BVH path may now bake. reconcile_bakes, which runs
        // first, copied the displaced ones for that bake (or none, while Cycles' shader still reports a displacement).
        undisplace(host, geometry);
        if (auto expected = expected_bake(scene, geometry); expected.apply) capture_bake(host, geometry, expected);
        continue;
      }
      // Cycles displaces a modified mesh again by itself, and a mesh whose displacement shader it tags.
      bool again = geometry->is_modified();
      bool host_again = it->second.object != object || it->second.motion != motion || (object && object->is_modified());
      for (Node *node : geometry->get_used_shaders()) {
        auto shader = static_cast<Shader *>(node);
        if (!shader->has_displacement) continue;
        again = again || shader->need_update_displacement;
        host_again = host_again || host.recompiled.count(shader) != 0;
      }
      if (again || (host_again && object)) {
        restore_displaced(host, geometry, it->second);
        it->second.object = object; it->second.motion = motion;
      }
      continue;
    }
    if (!displaced) continue;
    DisplacedRecord record; record.object = object; record.motion = motion;
    record.attributes = std::make_unique<AttributeSet>(geometry, geometry->attributes.prim);
    copy_attributes(geometry->attributes, *record.attributes);
    host.displaced[geometry] = std::move(record);
  }
  host.recompiled.clear();
}
const char *const volume_refusal = "Volume geometry was baked with its object transform by an earlier render and cannot be restored; "
                                   "delete the object and volume and create new ones to change its transform, motion, users or data";
// Called by every host function that edits a node, before the edit. A baked
// geometry gets its authored attributes back first, so the edit applies to
// authored data; edits that would need a baked volume restored are refused.
void before_edit(Host &host, Entry &entry, const char *socket_name) {
  if (entry.graph) return;
  // Cycles finalizes a shader graph once, for the displacement method of that time (ShaderManager::device_update: the bump
  // from displacement of Bump and Both), so a change of the method gets a new copy of the authored graph (prepare_graphs).
  if (auto shader = dynamic_cast<Shader *>(entry.node); shader && socket_name && std::strcmp(socket_name, "displacement_method") == 0)
    if (auto graph = host.graphs.find(shader); graph != host.graphs.end()) graph->second->dirty = true;
  if (dynamic_cast<Pass *>(entry.node) && socket_name && std::strcmp(socket_name, "mode") == 0) entry.pass_mode_edit = true;
  if (dynamic_cast<Camera *>(entry.node) && socket_name && std::strncmp(socket_name, "viewplane.", 10) == 0) host.explicit_viewplane = true;
  if (auto geometry = dynamic_cast<Geometry *>(entry.node)) {
    if (geometry->transform_applied && geometry->is_volume()) throw Failure(CBC_NOT_READY, volume_refusal);
    restore_splat_radius(host, geometry);
    if (geometry->transform_applied && bake_restorable(geometry)) unbake(host, geometry);
    undisplace(host, geometry);
  } else if (auto object = dynamic_cast<Object *>(entry.node)) {
    const std::string name = socket_name ? socket_name : "";
    Geometry *geometry = object->get_geometry();
    if ((name == "tfm" || name == "motion" || name == "geometry") && geometry && geometry->is_volume() && geometry->transform_applied)
      throw Failure(CBC_NOT_READY, volume_refusal);
  }
}
// Before a render: restore every baked geometry whose fresh-scene bake would
// differ, then capture the authored attributes of geometry this render may bake
// and the authored radius of splat clouds.
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
    capture_bake(host, geometry, expected);
  }
  host.edited_shaders.clear();
  keep_splat_radii(host);
}
// After a render (successful or not): drop captures Cycles did not bake.
void prune_bakes(Host &host) noexcept {
  for (auto it = host.bakes.begin(); it != host.bakes.end();) it = it->first->transform_applied ? std::next(it) : host.bakes.erase(it);
}
