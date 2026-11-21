/* sig_common.h - types for Sam2017_LoadUnsigned.asi */
#ifndef SIG_COMMON_H
#define SIG_COMMON_H

typedef unsigned char      u8;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed int         i32;
typedef signed long long   i64;
typedef __WCHAR_TYPE__     wchar_t;

/* One executable and the two spots where it refuses an asset for lacking a
   sufficient (EDITOR) signature. Both are left running the whole signature
   check; only the "reject" branch is redirected to the normal success path. */
typedef struct Target {
    const wchar_t *exeName;
    u32 timeDateStamp, sizeOfImage;

    /* main stream-credential check (CTStreamStack): the two instructions
         test r15d,r15d ; jne <ok>       (r15 = a sufficient signature was found)
       are replaced by an unconditional jump to that same <ok> label, so a
       missing/insufficient signature is no longer turned into the errors
       "'%1' isn't signed with sufficient credentials (%2)" and
       "Stream is signed with insufficient credentials." */
    u64 mainSite;                 /* at the "test r15d,r15d"            */
    u8  mainExpect[9];            /* original 9 bytes                   */
    u8  mainPatch[9];             /* jmp rel32 <ok> + NOP padding       */

    /* a second gate (a virtual verify on the current signature context) that
       raises the same "insufficient credentials" error; its conditional
       "proceed" branch (jbe) is made unconditional. 8-byte fingerprint
       (add eax,-2 ; cmp eax,2 ; jbe) ends at the patched 2 bytes. */
    u64 secSite;                  /* at the "jbe" (6 bytes of context precede it) */
    u8  secExpect[8];             /* add eax,-2 ; cmp eax,2 ; jbe rel8   */
    u8  secPatch[2];              /* jmp rel8 (same target)              */
} Target;

#endif
