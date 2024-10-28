import java.io.File;
import java.lang.instrument.ClassDefinition;
import java.lang.instrument.Instrumentation;

public class RedefineBootClassApp {
    public static void main(String args[]) throws Throwable {
        File bootJar = new File(args[0]);

        Class superCls = Thread.currentThread().getContextClassLoader().loadClass("BootSuper");
        System.out.println("BootSuper>> loader = " + superCls.getClassLoader());

        {
            BootSuper obj = (BootSuper)superCls.newInstance();
            System.out.println("(before transform) BootSuper>> doit() = " + obj.doit());
        }

        // Redefine the class
        Instrumentation instrumentation = InstrumentationRegisterClassFileTransformer.getInstrumentation();
        byte[] bytes = Util.getClassFileFromJar(bootJar, "BootSuper");
        Util.replace(bytes, "Hello", "HELLO");
        instrumentation.redefineClasses(new ClassDefinition(superCls, bytes));

        {
            BootSuper obj = (BootSuper)superCls.newInstance();
            String s = obj.doit();
            System.out.println("(after transform) BootSuper>> doit() = " + s);
            if (!s.equals("HELLO")) {
                throw new RuntimeException("BootSuper doit() should be HELLO but got " + s);
            }
        }

        Class childCls = Thread.currentThread().getContextClassLoader().loadClass("BootChild");
        System.out.println("BootChild>> loader = " + childCls.getClassLoader());


        {
            BootSuper obj = (BootSuper)childCls.newInstance();
            String s = obj.doit();
            System.out.println("(after transform) BootChild>> doit() = " + s);
            if (!s.equals("HELLO")) {
                throw new RuntimeException("BootChild doit() should be HELLO but got " + s);
            }
        }
    }
}
