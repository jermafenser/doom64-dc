.globl _array_fast_cpy

!
! void array_fast_cpy(uint32_t *dest, uint32_t *src, size_t n);
!
! r4 is an array of dest pointers, all should be 32 byte aligned
! r5 is an array of src pointers, all should be 8-byte aligned
! r6 is # of entries, each a single 32-byte block
! r2,r3 are scratch registers caller saved
! r0,r1 are return value caller saved
!
.align 16
_array_fast_cpy:
	mov.l	@r5+,	r3
	! Change to pair single-precision data
	fschg
1:
	mov.l	@r4+,	r1	! next dest address for loop
	fmov.d	@r3+,	dr0
	movca.l	r0,	@r1
	fmov.d	@r3+,	dr2
	add	#32,	r1
	fmov.d	@r3+,	dr4
	fmov.d	@r3+,	dr6
	mov.l	@r5+,	r3	! next src address for loop
	fmov.d	dr6,	@-r1
	dt	r6		! while(n--)
	fmov.d	dr4,	@-r1
	fmov.d	dr2,	@-r1
	bf.s	1b
	fmov.d	dr0,	@-r1
	rts
	fschg

.globl _single_fast_cpy
!
! void single_fast_cpy(uint32_t *dest, uint32_t *src);
!
! r4: dest (should be 32-byte aligned store queue address)
! r5: src (should be 8-byte aligned address)
!
   .align 16
_single_fast_cpy:
	fschg	! Change to pair single-precision data
	movca.l	r0,	@r4
	fmov.d	@r5+,	dr0
	fmov.d	@r5+,	dr2
	fmov.d	@r5+,	dr4
	fmov.d	@r5+,	dr6
	fmov.d	dr0,	@r4
	add	#8,	r4
	fmov.d	dr2,	@r4
	add	#8,	r4
	fmov.d	dr4,	@r4
	add	#8,	r4
	fmov.d	dr6,	@r4
	rts
	fschg
