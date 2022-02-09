# Using SwingSet to Analyze AppCDS Benefits on Windows

## Overview

This is a modified version of SwingSet that automatically loads all the demos during start-up. We can use it to approximate a large Swing app that may use many types of Swing components.

# Environment set-up

- I have modified SwingSet2.java (auto-loading) and TableDemo.java (compatibility with JDK 8)

- You can download [SwingSet.jar](https://github.com/iklam/jdk/raw/tmp-swingset-autoload/src/demo/share/jfc/SwingSet2/SwingSet.jar) from this directory, or use build.sh to build it yourself from source code.

- In cygwin, write a script like this:

        $ cat > run20.sh <<EOF
        for i in {1..20}; do
            "$@" -XX:+UseConcMarkSweepGC -XX:MinHeapFreeRatio=0 \
            -XX:+NeverActAsServerClassMachine -XX:MetaspaceSize=64m \
            -XX:MaxMetaspaceSize=192m -Xms8m -Xmx256m -jar SwingSet.jar &
        done
        EOF

- Set up AppCDS archive for SwingSet.jar:

        $ ./jdk-11.0.9.0.4/bin/java.exe -XX:DumpLoadedClassList=classlist -jar SwingSet.jar
        $ wc classlist
          3039   3039 109472 classlist
        $ ./jdk-11.0.9.0.4/bin/java.exe -Xshare:dump -cp SwingSet.jar \
            -XX:SharedClassListFile=classlist \
            -XX:SharedArchiveFile=SwingSet.jsa
        narrow_klass_base = 0x0000000800000000, narrow_klass_shift = 3
        Allocated temporary class space: 1073741824 bytes at 0x00000008c0000000
        Allocated shared space: 3221225472 bytes at 0x0000000800000000
        Loading classes to share ...
        Loading classes to share: done.
        Rewriting and linking classes ...
        Rewriting and linking classes: done
        Number of classes 3121
            instance classes   =  3041
            obj array classes  =    72
            type array classes =     8
        Updating ConstMethods ... done.
        Removing unshareable information ... done.
        Scanning all metaspace objects ...
        Allocating RW objects ...
        Allocating RO objects ...
        Relocating embedded pointers ...
        Relocating external roots ...
        Dumping symbol table ...
        Relocating SystemDictionary::_well_known_klasses[] ...
        Removing java_mirror ... done.
        mc  space:     14872 [  0.0% of total] out of     65536 bytes [ 22.7% used] at 0x0000000800000000
        rw  space:   9725072 [ 24.6% of total] out of   9764864 bytes [ 99.6% used] at 0x0000000800010000
        ro  space:  15456856 [ 39.2% of total] out of  15466496 bytes [ 99.9% used] at 0x0000000800960000
        md  space:      2560 [  0.0% of total] out of     65536 bytes [  3.9% used] at 0x0000000801820000
        od  space:  14075976 [ 35.7% of total] out of  14090240 bytes [ 99.9% used] at 0x0000000801830000
        total    :  39275336 [100.0% of total] out of  39452672 bytes [ 99.6% used]
        $ ls -l SwingSet.jsa
        -r-xr-xr-x+ 1 IKLAM-US+iklam IKLAM-US+None 39518208 Feb  8 23:02 SwingSet.jsa


# Running the tests and taking measurements

- I use Windows 10 -> Task Manager -> Performance -> Open Resource Monitor, and look at the "In Use" memory.
- Close all other applications such as browser, etc.
- Record "In Use" memory. This is column A.
- Run run20.sh to launch 20 concurrent copies of SwingSet. Wait until all demos are loaded (the screen shows the "Tree Demo"). Also watch Task Manager until CPU usage goes down to almost zero. 
- Record "In Use" memory. This is column B.
- Close all 20 copies of SwingSet.
- Record "In Use" memory. This is column C.

                A    B    C    D     E
        1    6374,8024,6356,1668, 83.4   bash run20.sh ./8-32/jdk1.8.0_331/bin/java.exe
        2    6323,8302,6323,1979, 99.0   bash run20.sh ./8-64/jdk1.8.0_331/bin/java.exe
        3    6265,8495,6375,2120,106.0   bash run20.sh ./jdk-11.0.9.0.4/bin/java.exe
        4    6210,8014,6182,1832, 91.6   bash run20.sh ./jdk-11.0.9.0.4/bin/java.exe -Xshare:auto -XX:SharedArchiveFile=SwingSet.jsa

- Column D = B - C (memory used by 20 SwingSet processes)
- Column E = D / 20 (memory used by each SwingSet process)
- Columns A-E are all in megabytes.

# Benefits of AppCDS

From the CDS logs above, you can see that the "ro" space (read only) is about 15MB. This memory can be shared across the SwingSet processes. This roughly corresponds to the differences of (E3 - E4) = (106.0 - 91.6) = 14.4MB.

Note that 3121 classes are shared.

        Number of classes 3121
        ...
        mc  space:     14872 [  0.0% of total] out of     65536 bytes [ 22.7% used] at 0x0000000800000000
        rw  space:   9725072 [ 24.6% of total] out of   9764864 bytes [ 99.6% used] at 0x0000000800010000
        ro  space:  15456856 [ 39.2% of total] out of  15466496 bytes [ 99.9% used] at 0x0000000800960000
        md  space:      2560 [  0.0% of total] out of     65536 bytes [  3.9% used] at 0x0000000801820000
        od  space:  14075976 [ 35.7% of total] out of  14090240 bytes [ 99.9% used] at 0x0000000801830000
        total    :  39275336 [100.0% of total] out of  39452672 bytes [ 99.6% used]


I've captured the Resource Manager screenshots for the above cases 3 vs 4

The "Shareable" footprint increases from about 28MB to about 56MB. This corresponds to the sum of the mc/rw/ro/md spaces (about 25MB). The "od" space is not used (it's for supporting JVMTI ClassFileLoadHook, so it will be mapped only if you have enabled JVMTI).

- case 3: [http://cr.openjdk.java.net/~iklam/misc/20-swingset-jdk11-noCDS.png](http://cr.openjdk.java.net/~iklam/misc/20-swingset-jdk11-noCDS.png)

    ![CDS disabled](http://cr.openjdk.java.net/~iklam/misc/20-swingset-jdk11-noCDS.png)

- case 4: [http://cr.openjdk.java.net/~iklam/misc/20-swingset-jdk11-AppCDS.png](http://cr.openjdk.java.net/~iklam/misc/20-swingset-jdk11-AppCDS.png)

    ![AppCDS enabled](http://cr.openjdk.java.net/~iklam/misc/20-swingset-jdk11-AppCDS.png)


