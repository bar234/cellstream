#ifndef __SYNCHRO_ATOMIC_H__
#define __SYNCHRO_ATOMIC_H__


#define EIEIO_ON_SMP    "eieio\n"
#define ISYNC_ON_SMP    "\n\tisync"



static __inline__ unsigned long
 __xchg_u32(volatile unsigned int *m, unsigned long val)
 {
         unsigned long dummy;

         __asm__ __volatile__(
         EIEIO_ON_SMP
 "1:     lwarx %0,0,%3           # __xchg_u32\n\
         stwcx. %2,0,%3\n\
 2:      bne- 1b"
         ISYNC_ON_SMP
         : "=&r" (dummy), "=m" (*m)
         : "r" (val), "r" (m)
         : "cc", "memory");

         return (dummy);
}

/*static __inline__ unsigned long
atomic_add2(volatile unsigned int * __mem, int __val)
{
	unsigned long dummy;

    __asm__ __volatile__ (
      "\n$Ladd_%=:\n\t"
      "ldl_l  %0,%2\n\t"
      "addl   %0,%3,%0\n\t"
      "stl_c  %0,%1\n\t"
      "beq    %0,$Ladd_%=\n\t"
      "mb"
      : "=&r"(dummy), "=m"(*__mem)
      : "m" (*__mem), "r"(__val));

	return (dummy);
  }*/
#endif

