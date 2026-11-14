/* fficonfig.h for libffi on Ulix (AArch64, musl): what libffi's configure would have found. */
#define HAVE_ALLOCA_H 1
#define HAVE_MEMCPY 1
#define HAVE_MMAP 1
#define HAVE_MMAP_ANON 1
#define STDC_HEADERS 1
#define HAVE_AS_CFI_PSEUDO_OP 1
#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1
#define EH_FRAME_FLAGS "a"
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name) .hidden name
#else
#define FFI_HIDDEN __attribute__ ((visibility ("hidden")))
#endif
