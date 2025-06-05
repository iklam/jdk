/*

$ javac FieldSearchTest.java
$ jar cvf FieldSearchTest.jar FieldSearchTest.class
$ java -cp FieldSearchTest.jar -XX:ArchiveClassesAtExit=FieldSearchTest.jsa FieldSearchTest
$ java -cp FieldSearchTest.jar -Xlog:cds=debug -XX:SharedArchiveFile=FieldSearchTest.jsa FieldSearchTest
[..]
 - ---- field info search table:
   [0] #22,#23 = new__field_name00:I @ 0x70b122df5018,0x70b122556708
   [2] #40,#23 = start:I @ 0x70b12256ada0,0x70b122556708
   [4] #24,#23 = new__field_name01:I @ 0x70b122df5030,0x70b122556708
   [6] #25,#23 = new__field_name02:I @ 0x70b122df5048,0x70b122556708
   [8] #26,#23 = new__field_name03:I @ 0x70b122df5060,0x70b122556708
   [10] #27,#23 = new__field_name04:I @ 0x70b122df5078,0x70b122556708
   [12] #28,#23 = new__field_name05:I @ 0x70b122df5090,0x70b122556708
   [14] #29,#23 = new__field_name06:I @ 0x70b122df50a8,0x70b122556708
   [16] #30,#23 = new__field_name07:I @ 0x70b122df50c0,0x70b122556708
   [18] #31,#23 = new__field_name08:I @ 0x70b122df50d8,0x70b122556708
   [20] #32,#23 = new__field_name09:I @ 0x70b122df50f0,0x70b122556708
   [22] #33,#23 = new__field_name10:I @ 0x70b122df5108,0x70b122556708
   [24] #34,#23 = new__field_name11:I @ 0x70b122df5120,0x70b122556708
   [26] #35,#23 = new__field_name12:I @ 0x70b122df5138,0x70b122556708
   [28] #36,#23 = new__field_name13:I @ 0x70b122df5150,0x70b122556708
   [30] #37,#23 = new__field_name14:I @ 0x70b122df5168,0x70b122556708
   [32] #38,#23 = new__field_name15:I @ 0x70b122df5180,0x70b122556708
   [34] #39,#23 = new__field_name16:I @ 0x70b122df5198,0x70b122556708
#
# A fatal error has been detected by the Java Runtime Environment:
#
#  Internal Error (/jdk3/abe/open/src/hotspot/share/utilities/packedTable.cpp:105), pid=972196, tid=972197
#  assert(comparator.compare_to(key) < 0) failed: not sorted


 */

public class FieldSearchTest {
    int new__field_name00;
    int new__field_name01;
    int new__field_name02;
    int new__field_name03;
    int new__field_name04;
    int new__field_name05;
    int new__field_name06;
    int new__field_name07;
    int new__field_name08;
    int new__field_name09;
    int new__field_name10;
    int new__field_name11;
    int new__field_name12;
    int new__field_name13;
    int new__field_name14;
    int new__field_name15;
    int new__field_name16;
    int start;             // Symbol "start" is guaranteed to be in the base CDS archive (from Thread.start()).
    public static void main(String args[]) {
        System.out.println("FieldSearchTest");
    }
}
