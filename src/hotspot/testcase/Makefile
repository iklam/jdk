# HOWTO:
#
#     export JAVA_HOME=......
#     make run
#     make run-parallel QUIET=1
#
# To stop the parallel runs:
#
#     touch stop.now
#
# If you change JAVA_HOME to point to a different build, do this
# so that ZGCBaseArchive.jsa is rebuilt.
#
#     make clean
#     ....

JAVA  = ${JAVA_HOME}/bin/java
JAR   = ${JAVA_HOME}/bin/jar
JAVAC = ${JAVA_HOME}/bin/javac

LotsUnload.jar: LotsUnload.java
	rm -rf classes
	mkdir classes
	${JAVAC} -d classes LotsUnload.java
	${JAR} cvf LotsUnload.jar -C classes .

ZGCBaseArchive.jsa:
	${JAVA} -Xshare:dump -XX:SharedArchiveFile=ZGCBaseArchive.jsa -XX:+UseZGC

ifndef QUIET
LOGS    = -Xlog:class+unload -Xlog:cds -Xlog:cds+class=debug
endif
ifndef ARCHIVE
ARCHIVE = dyn.jsa
endif
ifndef NUMCLS
NUMCLS = 512
endif

run: ZGCBaseArchive.jsa LotsUnload.jar
	@echo -n .$(NUMCLS)
	@${JAVA} -cp LotsUnload.jar -XX:ArchiveClassesAtExit=${ARCHIVE} -Xmx64m -Xms32m -XX:+UseZGC \
	    -XX:SharedArchiveFile=ZGCBaseArchive.jsa \
	    ${LOGS} \
	    LotsUnload 16 $(NUMCLS)

run-parallel: ZGCBaseArchive.jsa LotsUnload.jar
	@echo ----------------------------------------
	@echo To stop, do not type Ctrl-C. Instead:
	@echo "    touch stop.now"
	@echo ----------------------------------------
	rm -f stop.now
	ulimit -c unlimited; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12; do \
	    (while $(MAKE) -s run ARCHIVE=dyn$$i.jsa NUMCLS=$$(expr 117 + $$i \* 29); do if test -f stop.now; then break; fi; done; touch stop.now) & \
	done; \
	wait

clean:
	rm -rf *.jsa *~ *.class *.jar classes stop.now


