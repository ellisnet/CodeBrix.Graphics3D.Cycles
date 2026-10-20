// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// SPDX-License-Identifier: Apache-2.0
ccl_device_inline void integrator_state_copy_only(KernelGlobals kg,
                                                  ConstIntegratorState to_state,
                                                  ConstIntegratorState state)
{
  int index;

  /* Rely on the compiler to optimize out unused assignments and `while(false)`'s. */

#  define KERNEL_STRUCT_BEGIN(name) \
    index = 0; \
    do {

#  define KERNEL_STRUCT_MEMBER(parent_struct, type, name, feature) \
    if (kernel_integrator_state.parent_struct.name != nullptr) { \
      kernel_integrator_state.parent_struct.name[to_state] = \
          kernel_integrator_state.parent_struct.name[state]; \
    }

#  ifdef __INTEGRATOR_GPU_PACKED_STATE__
#    define KERNEL_STRUCT_BEGIN_PACKED(parent_struct, feature) \
      KERNEL_STRUCT_BEGIN(parent_struct) \
      KERNEL_STRUCT_MEMBER(parent_struct, packed_##parent_struct, packed, feature)
#    define KERNEL_STRUCT_MEMBER_PACKED(parent_struct, type, name, feature)
#  else
#    define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#    define KERNEL_STRUCT_BEGIN_PACKED(parent_struct, feature) KERNEL_STRUCT_BEGIN(parent_struct)
#  endif

#  define KERNEL_STRUCT_ARRAY_MEMBER(parent_struct, type, name, feature) \
    if (kernel_integrator_state.parent_struct[index].name != nullptr) { \
      kernel_integrator_state.parent_struct[index].name[to_state] = \
          kernel_integrator_state.parent_struct[index].name[state]; \
    }

#  define KERNEL_STRUCT_END(name) \
    } \
    while (false) \
      ;

#  define KERNEL_STRUCT_END_ARRAY(name, cpu_array_size, gpu_array_size) \
    ++index; \
    } \
    while (index < gpu_array_size) \
      ;

#  define KERNEL_STRUCT_VOLUME_STACK_SIZE kernel_data.volume_stack_size

#  include "kernel/integrator/state_template.h"

#  undef KERNEL_STRUCT_BEGIN
#  undef KERNEL_STRUCT_BEGIN_PACKED
#  undef KERNEL_STRUCT_MEMBER
#  undef KERNEL_STRUCT_MEMBER_PACKED
#  undef KERNEL_STRUCT_ARRAY_MEMBER
#  undef KERNEL_STRUCT_END
#  undef KERNEL_STRUCT_END_ARRAY
#  undef KERNEL_STRUCT_VOLUME_STACK_SIZE
}

ccl_device_inline void integrator_state_move(KernelGlobals kg,
                                             ConstIntegratorState to_state,
                                             ConstIntegratorState state)
{
  integrator_state_copy_only(kg, to_state, state);

  INTEGRATOR_STATE_WRITE(state, path, queued_kernel) = 0;

#  ifdef __MNEE__
  if (INTEGRATOR_STATE(to_state, path, mnee) & PATH_MNEE_SAMPLED) {
    const IntegratorShadowState slot = INTEGRATOR_STATE(to_state, path, mnee_shadow_state);
    integrator_state_write_mnee_shadow_owner(slot, to_state);
  }
#  endif
}

ccl_device_inline void integrator_shadow_state_copy_only(KernelGlobals kg,
                                                         ConstIntegratorShadowState to_state,
                                                         ConstIntegratorShadowState state)
{
  int index;

  /* Rely on the compiler to optimize out unused assignments and `while(false)`'s. */

#  define KERNEL_STRUCT_BEGIN(name) \
    index = 0; \
    do {

#  define KERNEL_STRUCT_MEMBER(parent_struct, type, name, feature) \
    if (kernel_integrator_state.parent_struct.name != nullptr) { \
      kernel_integrator_state.parent_struct.name[to_state] = \
          kernel_integrator_state.parent_struct.name[state]; \
    }

#  ifdef __INTEGRATOR_GPU_PACKED_STATE__
#    define KERNEL_STRUCT_BEGIN_PACKED(parent_struct, feature) \
      KERNEL_STRUCT_BEGIN(parent_struct) \
      KERNEL_STRUCT_MEMBER(parent_struct, type, packed, feature)
#    define KERNEL_STRUCT_MEMBER_PACKED(parent_struct, type, name, feature)
#  else
#    define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#    define KERNEL_STRUCT_BEGIN_PACKED(parent_struct, feature) KERNEL_STRUCT_BEGIN(parent_struct)
#  endif

#  define KERNEL_STRUCT_ARRAY_MEMBER(parent_struct, type, name, feature) \
    if (kernel_integrator_state.parent_struct[index].name != nullptr) { \
      kernel_integrator_state.parent_struct[index].name[to_state] = \
          kernel_integrator_state.parent_struct[index].name[state]; \
    }

#  define KERNEL_STRUCT_END(name) \
    } \
    while (false) \
      ;

#  define KERNEL_STRUCT_END_ARRAY(name, cpu_array_size, gpu_array_size) \
    ++index; \
    } \
    while (index < gpu_array_size) \
      ;

#  define KERNEL_STRUCT_VOLUME_STACK_SIZE kernel_data.volume_stack_size

#  include "kernel/integrator/shadow_state_template.h"

#  undef KERNEL_STRUCT_BEGIN
#  undef KERNEL_STRUCT_BEGIN_PACKED
#  undef KERNEL_STRUCT_MEMBER
#  undef KERNEL_STRUCT_MEMBER_PACKED
#  undef KERNEL_STRUCT_ARRAY_MEMBER
#  undef KERNEL_STRUCT_END
#  undef KERNEL_STRUCT_END_ARRAY
#  undef KERNEL_STRUCT_VOLUME_STACK_SIZE
}

ccl_device_inline void integrator_shadow_state_move(KernelGlobals kg,
                                                    ConstIntegratorState to_state,
                                                    ConstIntegratorState state)
{
  integrator_shadow_state_copy_only(kg, to_state, state);

  INTEGRATOR_STATE_WRITE(state, shadow_path, queued_kernel) = 0;

#  ifdef __MNEE__
  if (INTEGRATOR_STATE(to_state, shadow_path, queued_kernel) ==
      DEVICE_KERNEL_INTEGRATOR_SHADOW_PATH_MNEE_PENDING)
  {
    const IntegratorState main_state = integrator_state_read_mnee_shadow_owner(to_state);
    INTEGRATOR_STATE_WRITE(main_state, path, mnee_shadow_state) = (int)to_state;
  }
#  endif
}

