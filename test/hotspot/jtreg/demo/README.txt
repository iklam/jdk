https://bugs.openjdk.java.net/browse/CODETOOLS-7902847

Summary:
=======

JTREG should not use the class directory of a "test" to compile the classes of a "library"

Reproducing
===========



/jdk/open/test/hotspot/jtreg/demo$ env JAVA_HOME=/home/iklam/jdk/official/jdk15 \
    /home/iklam/jdk/tools/jtreg/5.1-b01/bin/jtreg \
    -J-Djavatest.maxOutputSize=10000000 \
    -conc:1 \
    -testjdk:/home/iklam/jdk/bld/ken/images/jdk \
    -compilejdk:/home/iklam/jdk/bld/ken/images/jdk \
    -verbose:2 \
    -timeout:4.0 \
    -agentvm \
    -exclude:/jdk2/ken/closed/test/hotspot/jtreg/ProblemList.txt \
    -exclude:/jdk2/ken/open/test/hotspot/jtreg/ProblemList.txt \
    -nativepath:/home/iklam/jdk/bld/ken/images/jdk/../../images/test/hotspot/jtreg/native \
    -vmoptions:-XX:MaxRAM=8g \
    -nr \
    -w \
    /home/iklam/tmp/jtreg/work \
    -r \
    /home/iklam/tmp/jtreg/report/ \
    \
    Test1.java Test2.java
Directory "/home/iklam/tmp/jtreg/work" not found: creating
runner starting test: demo/Test1.java
runner finished test: demo/Test1.java
Passed. Execution successful
runner starting test: demo/Test2.java
runner finished test: demo/Test2.java
Error. Agent communication error: java.io.EOFException; check console log for any additional details
Test results: passed: 1; error: 1
Results written to /jdk2/tmp/jtreg/work
Error: Some tests failed or other problems occurred.


~/tmp/jtreg$ find . -name \*.class | xargs ls -ltr
-rw-r--r-- 1 iklam dba  1469 Mar 12 22:41 ./work/extraPropDefns/bootClasses/sun/hotspot/parser/DiagnosticCommand$DiagnosticArgumentType.class
-rw-r--r-- 1 iklam dba  1427 Mar 12 22:41 ./work/extraPropDefns/bootClasses/sun/hotspot/parser/DiagnosticCommand.class
-rw-r--r-- 1 iklam dba  2016 Mar 12 22:41 ./work/extraPropDefns/bootClasses/sun/hotspot/gc/GC.class
-rw-r--r-- 1 iklam dba   354 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/WhiteBox$WhiteBoxPermission.class
-rw-r--r-- 1 iklam dba 18002 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/WhiteBox.class
-rw-r--r-- 1 iklam dba  1713 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/cpuinfo/CPUInfo.class
-rw-r--r-- 1 iklam dba  1858 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/NMethod.class
-rw-r--r-- 1 iklam dba  2319 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/CodeBlob.class
-rw-r--r-- 1 iklam dba   607 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType$3.class
-rw-r--r-- 1 iklam dba   573 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType$2.class
-rw-r--r-- 1 iklam dba   570 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType$1.class
-rw-r--r-- 1 iklam dba  3543 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType.class
-rw-r--r-- 1 iklam dba  2994 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/Compiler.class
-rw-r--r-- 1 iklam dba  9226 Mar 12 22:42 ./work/extraPropDefns/classes/jdk/test/lib/Platform.class
-rw-r--r-- 1 iklam dba   478 Mar 12 22:42 ./work/extraPropDefns/classes/jdk/test/lib/Container.class
-rw-r--r-- 1 iklam dba  1669 Mar 12 22:42 ./work/extraPropDefns/classes/requires/VMProps$SafeMap.class
-rw-r--r-- 1 iklam dba   660 Mar 12 22:42 ./work/extraPropDefns/classes/requires/VMProps$1.class
-rw-r--r-- 1 iklam dba 13566 Mar 12 22:42 ./work/extraPropDefns/classes/requires/VMProps.class
-rw-r--r-- 1 iklam dba  1207 Mar 12 22:42 ./work/patches/java.base/java/lang/JTRegModuleHelper.class
-rw-r--r-- 1 iklam dba   243 Mar 12 22:42 ./work/classes/demo/Test1.d/Test1.class
-rw-r--r-- 1 iklam dba   182 Mar 12 22:42 ./work/classes/demo/Test1.d/LibA.class
-rw-r--r-- 1 iklam dba   241 Mar 12 22:42 ./work/classes/test/hotspot/jtreg/demo/lib/LibB.class


~/tmp/jtreg$ find . -type f | xargs ls -ltr
-rw-r--r-- 1 iklam dba    80 Mar 12 22:41 ./work/jtData/wdinfo
-rw-r--r-- 1 iklam dba   152 Mar 12 22:41 ./work/jtData/testsuite
-rw-r--r-- 1 iklam dba    14 Mar 12 22:41 ./work/jtData/logfile.log.log.index
-rw-r--r-- 1 iklam dba  1469 Mar 12 22:41 ./work/extraPropDefns/bootClasses/sun/hotspot/parser/DiagnosticCommand$DiagnosticArgumentType.class
-rw-r--r-- 1 iklam dba  1427 Mar 12 22:41 ./work/extraPropDefns/bootClasses/sun/hotspot/parser/DiagnosticCommand.class
-rw-r--r-- 1 iklam dba  2016 Mar 12 22:41 ./work/extraPropDefns/bootClasses/sun/hotspot/gc/GC.class
-rw-r--r-- 1 iklam dba   354 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/WhiteBox$WhiteBoxPermission.class
-rw-r--r-- 1 iklam dba 18002 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/WhiteBox.class
-rw-r--r-- 1 iklam dba  1713 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/cpuinfo/CPUInfo.class
-rw-r--r-- 1 iklam dba  1858 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/NMethod.class
-rw-r--r-- 1 iklam dba  2319 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/CodeBlob.class
-rw-r--r-- 1 iklam dba   607 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType$3.class
-rw-r--r-- 1 iklam dba   573 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType$2.class
-rw-r--r-- 1 iklam dba   570 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType$1.class
-rw-r--r-- 1 iklam dba  3543 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/BlobType.class
-rw-r--r-- 1 iklam dba  2994 Mar 12 22:42 ./work/extraPropDefns/bootClasses/sun/hotspot/code/Compiler.class
-rw-r--r-- 1 iklam dba  9226 Mar 12 22:42 ./work/extraPropDefns/classes/jdk/test/lib/Platform.class
-rw-r--r-- 1 iklam dba   478 Mar 12 22:42 ./work/extraPropDefns/classes/jdk/test/lib/Container.class
-rw-r--r-- 1 iklam dba  1669 Mar 12 22:42 ./work/extraPropDefns/classes/requires/VMProps$SafeMap.class
-rw-r--r-- 1 iklam dba   660 Mar 12 22:42 ./work/extraPropDefns/classes/requires/VMProps$1.class
-rw-r--r-- 1 iklam dba 13566 Mar 12 22:42 ./work/extraPropDefns/classes/requires/VMProps.class
-rw-r--r-- 1 iklam dba   223 Mar 12 22:42 ./work/jtreg.policy
-rw-r--r-- 1 iklam dba  1207 Mar 12 22:42 ./work/patches/java.base/java/lang/JTRegModuleHelper.class
-rw-r--r-- 1 iklam dba   447 Mar 12 22:42 ./work/jtData/jtreg.version
-rw-r--r-- 1 iklam dba   243 Mar 12 22:42 ./work/classes/demo/Test1.d/Test1.class
-rw-r--r-- 1 iklam dba   182 Mar 12 22:42 ./work/classes/demo/Test1.d/LibA.class
-rw-r--r-- 1 iklam dba   241 Mar 12 22:42 ./work/classes/test/hotspot/jtreg/demo/lib/LibB.class
-rw-r--r-- 1 iklam dba 12812 Mar 12 22:42 ./work/demo/Test1.jtr
-rw-r--r-- 1 iklam dba   734 Mar 12 22:42 ./work/jtData/agentServer.2.trace
-rw-r--r-- 1 iklam dba   452 Mar 12 22:42 ./work/jtData/logfile.log
-rw-r--r-- 1 iklam dba   159 Mar 12 22:42 ./work/jtData/lastRun.txt
-rw-r--r-- 1 iklam dba  6063 Mar 12 22:42 ./work/demo/Test2.jtr
-rw-r--r-- 1 iklam dba    89 Mar 12 22:42 ./work/jtData/log.txt
-rw-r--r-- 1 iklam dba   128 Mar 12 22:42 ./work/jtData/logfile.log.rec.index
-rw-r--r-- 1 iklam dba   816 Mar 12 22:42 ./work/jtData/harness.trace
-rw-r--r-- 1 iklam dba   536 Mar 12 22:42 ./work/jtData/agentServer.1.trace
-rw-r--r-- 1 iklam dba  3430 Mar 12 22:42 ./work/jtData/agent.trace
-rw-r--r-- 1 iklam dba   815 Mar 12 22:42 ./work/jtData/agent.summary
-rw-r--r-- 1 iklam dba   179 Mar 12 22:42 ./work/jtData/ResultCache2.jtw

===============================================================================

The problem can be shown in Test1.jtr:

---> note that /jdk2/tmp/jtreg/work/classes/demo/Test1.d is given in the -classpath for javac. This is incorrect.
     It makes classes in the @library depend on classes in the test case.

cd /jdk2/tmp/jtreg/work/scratch && \\
DISPLAY=:2 \\
HOME=/home/iklam \\
JTREG_COMPILEJDK=/home/iklam/jdk/bld/ken/images/jdk \\
JTREG_RETAIN=-retain:all \\
LANG=en_US.UTF-8 \\
PATH=/bin:/usr/bin:/usr/sbin \\
    /home/iklam/jdk/bld/ken/images/jdk/bin/javac \\
        -J-XX:MaxRAM=8g \\
        -J-Djava.library.path=/home/iklam/jdk/bld/ken/images/jdk/../../images/test/hotspot/jtreg/native \\
        -J-Dtest.vm.opts=-XX:MaxRAM=8g \\
        -J-Dtest.tool.vm.opts=-J-XX:MaxRAM=8g \\
        -J-Dtest.compiler.opts= \\
        -J-Dtest.java.opts= \\
        -J-Dtest.jdk=/home/iklam/jdk/bld/ken/images/jdk \\
        -J-Dcompile.jdk=/home/iklam/jdk/bld/ken/images/jdk \\
        -J-Dtest.timeout.factor=4.0 \\
        -J-Dtest.nativepath=/home/iklam/jdk/bld/ken/images/jdk/../../images/test/hotspot/jtreg/native \\
        -J-Dtest.root=/jdk2/ken/open/test/hotspot/jtreg \\
        -J-Dtest.name=demo/Test1.java \\
        -J-Dtest.file=/jdk2/ken/open/test/hotspot/jtreg/demo/Test1.java \\
        -J-Dtest.src=/jdk2/ken/open/test/hotspot/jtreg/demo \\
        -J-Dtest.src.path=/jdk2/ken/open/test/hotspot/jtreg/demo:/jdk2/ken/open/test/hotspot/jtreg/demo/lib \\
        -J-Dtest.classes=/jdk2/tmp/jtreg/work/classes/demo/Test1.d \\
        -J-Dtest.class.path=/jdk2/tmp/jtreg/work/classes/demo/Test1.d:/jdk2/tmp/jtreg/work/classes/test/hotspot/jtreg/demo/lib \\
        -J-Dtest.class.path.prefix=/jdk2/tmp/jtreg/work/classes/demo/Test1.d:/jdk2/ken/open/test/hotspot/jtreg/demo:/jdk2/tmp/jtreg/work/classes/test/hotspot/jtreg/demo/lib \\
        -d /jdk2/tmp/jtreg/work/classes/test/hotspot/jtreg/demo/lib \\
        -sourcepath /jdk2/ken/open/test/hotspot/jtreg/demo/lib \\
        -classpath /jdk2/tmp/jtreg/work/classes/demo/Test1.d:/jdk2/tmp/jtreg/work/classes/test/hotspot/jtreg/demo/lib /jdk2/ken/open/test/hotspot/jtreg/demo/lib/LibB.java
