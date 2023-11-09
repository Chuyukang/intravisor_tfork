#ifndef CHERI_BUILTIN_H
#define CHERI_BUILTIN_H

// borrowed from CheriBSD CMakelists.txt
// Pretend that we have capabilities in CLion
#define __CHERI__
#define __has_feature(x) __fake_has_feature_##x
#define __fake_has_feature_capabilities 1
#define __fake_has_feature_nullability 1
// But we don't really so define __capability to nothing
#define __capability
// Same thing for __(u)intcap_t
typedef unsigned long __uintcap_t;
typedef long __intcap_t;
// And provide the builtins for code completion
long __builtin_cheri_length_get(const void* __capability);
long __builtin_cheri_base_get(const void* __capability);
long __builtin_cheri_offset_get(const void* __capability);
long __builtin_cheri_address_get(const void* __capability);
long __builtin_cheri_flags_get(const void* __capability);
long __builtin_cheri_perms_get(const void* __capability);
long __builtin_cheri_sealed_get(const void* __capability);
long __builtin_cheri_tag_get(const void* __capability);
long __builtin_cheri_type_get(const void* __capability);
void* __capability __builtin_cheri_perms_and(const void* __capability, long);
void* __capability __builtin_cheri_tag_clear(const void* __capability);
void* __capability __builtin_cheri_offset_increment(const void* __capability, long);
void* __capability __builtin_cheri_offset_set(const void* __capability, long);
void* __capability __builtin_cheri_address_set(const void* __capability, long);
void* __capability __builtin_cheri_flags_set(const void* __capability, long);
void* __capability __builtin_cheri_seal(const void* __capability, void* __capability);
void* __capability __builtin_cheri_unseal(const void* __capability, void* __capability);
void __builtin_cheri_perms_check(const void* __capability, long);
void __builtin_cheri_type_check(const void* __capability, const void* __capability);
void* __capability __builtin_cheri_global_data_get(void);
void* __capability __builtin_cheri_program_counter_get(void);
void* __capability __builtin_cheri_stack_get(void);
void* __capability __builtin_cheri_bounds_set(const void* __capability, long);
void* __capability __builtin_cheri_bounds_set_exact(const void* __capability, long);
unsigned long __builtin_cheri_representable_alignment_mask(unsigned long);
unsigned long __builtin_cheri_round_representable_length(unsigned long);
// Also define some compiler defines:
#define __CHERI_ADDRESS_BITS__ 64
#define __CHERI_CAPABILITY_WIDTH__ 128
#define __CHERI_CAP_PERMISSION_ACCESS_SYSTEM_REGISTERS__ 1024
#define __CHERI_CAP_PERMISSION_GLOBAL__ 1
#define __CHERI_CAP_PERMISSION_PERMIT_CCALL__ 256
#define __CHERI_CAP_PERMISSION_PERMIT_EXECUTE__ 2
#define __CHERI_CAP_PERMISSION_PERMIT_LOAD_CAPABILITY__ 16
#define __CHERI_CAP_PERMISSION_PERMIT_LOAD__ 4
#define __CHERI_CAP_PERMISSION_PERMIT_SEAL__ 128
#define __CHERI_CAP_PERMISSION_PERMIT_STORE_CAPABILITY__ 32
#define __CHERI_CAP_PERMISSION_PERMIT_STORE_LOCAL__ 64
#define __CHERI_CAP_PERMISSION_PERMIT_STORE__ 8
#define __CHERI_CAP_PERMISSION_PERMIT_UNSEAL__ 512
#define __SIZEOF_CHERI_CAPABILITY__ 16
#define __SIZEOF_UINTCAP__ 16
#define __SIZEOF_INTCAP__ 16

#endif
