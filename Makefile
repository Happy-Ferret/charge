################################################################
# * Charge System Management Framework 
# * Sccsid @(#)Makefile	1.1 (Charge) 22/06/14
################################################################

SUBDIR=libsvc \
		svc.restartd \

preclean:
	rm -rf proto

clean: preclean

prepackage:
	mkdir -p proto/libexec/svc
package: all prepackage
	fakeroot make install DESTDIR=${PWD}/proto


.include <bsd.subdir.mk>
