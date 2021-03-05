
#ifndef __ngpt_compiler_h__
#define __ngpt_compiler_h__

#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define likely(p) (p)
#define unlikely(p) (p)
#else
#define likely(p) __builtin_expect((p), 1)
#define unlikely(p) __builtin_expect((p), 0)
#endif

#endif /* __ngpt_compiler_h__ */
