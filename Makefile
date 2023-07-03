RSYNC_DEBUG=	${WITH_DEBUG:D-n}
RSYNC=	rsync -aFv ${RSYNC_DEBUG} --no-g --no-p --delete \
	"--filter=:- .gitignore" \
	--exclude=.vs

fetch:
	@${RSYNC} --exclude=.git ${DESTDIR}/ ./

push:
	@${RSYNC} ./ ${DESTDIR}/

.include "Makefile.local"
