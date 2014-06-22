################################################################
# * Charge System Management Framework 
# * Sccsid @(#)charge.prog.mk	1.1 (Charge) 22/06/14
################################################################

# # # # # # # #
# Includes  # #
# # # # # # # #
CFLAGS+=-I${PWD}/../libsvc -I ${PWD}/../hdr

# # # # # # # #
# Libraries # #
# # # # # # # #
LDFLAGS+=-L${PWD}/../libsvc

LIBSVC=${PWD}/../libsvc/libsvc.a

libclean: 
	rm -f *.o *.a *.po
