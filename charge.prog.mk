################################################################
# * Charge System Management Framework 
# * Sccsid @(#)charge.prog.mk	1.2 (Charge) 22/06/14
################################################################

# # # # # # # #
# CFlags  # # #
# # # # # # # #
.ifdef (DEBUG)
  CFLAGS = -g -O0
.endif
CFLAGS+=-I${PWD}/../libsvc -I ${PWD}/../hdr

# # # # # # # #
# Libraries # #
# # # # # # # #
LDFLAGS+=-L${PWD}/../libsvc

LIBSVC=${PWD}/../libsvc/libsvc.a

libclean: 
	rm -f *.o *.a *.po
