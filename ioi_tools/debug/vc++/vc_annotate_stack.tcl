#
# Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.  Oracle designates this
# particular file as subject to the "Classpath" exception as provided
# by Oracle in the LICENSE file that accompanied this code.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#


# Annotate a Windows (vc++) stack dump with symbolic names
#
# tclsh vc_annotate_stack.tcl <jvm.map> <hs_err.log>
#
# You need to download the correct version of jvm.map that matches with the JDK in your crash.
#
# tclsh vc_annotate_stack.tcl ./jdk-12/fastdebug/bin/server/jvm.map hs_err_pid33084.log
#
#    Stack: [0x000000081c4a0000,0x000000081c5a0000]
#    Native frames: (J=compiled Java code, j=interpreted, Vv=VM code, C=native code)
#    V  [jvm.dll+00b6df21] = 00b6ce30+ 241 os::platform_print_native_stack
#    V  [jvm.dll+00d8f65c] = 00d8d7e0+3708 VMError::report
#    V  [jvm.dll+00d91024] = 00d8f7d0+2132 VMError::report_and_die
#    V  [jvm.dll+00d916e4] = 00d90680+ 100 VMError::report_and_die
#    V  [jvm.dll+0059344e] = 005923d0+ 126 report_vm_error
#    V  [jvm.dll+00aeb7fe] = 00aea7a0+  94 Method::set_interpreter_entry
#    V  [jvm.dll+00ae2869] = 00ae1780+ 233 Method::Method
#    V  [jvm.dll+004a84f6] = 004a5b20+6614 ClassFileParser::parse_method
#    V  [jvm.dll+004a8e55] = 004a7a90+ 965 ClassFileParser::parse_methods
#    V  [jvm.dll+004a9d4a] = 004a80a0+3242 ClassFileParser::parse_stream
#    V  [jvm.dll+00497ac8] = 004963c0+1800 ClassFileParser::ClassFileParser
#    V  [jvm.dll+0099efe2] = 0099de10+ 466 KlassFactory::create_from_stream
#    V  [jvm.dll+004b414e] = 004b2f80+ 462 ClassLoader::load_class
#    V  [jvm.dll+00ce65a9] = 00ce5330+ 633 SystemDictionary::load_instance_class
#    V  [jvm.dll+00ce874f] = 00ce6d60+2543 SystemDictionary::resolve_instance_class_or_null
#    V  [jvm.dll+00ce9c19] = 00ce89d0+ 585 SystemDictionary::resolve_wk_klasses_until
#    V  [jvm.dll+00ce8de9] = 00ce7ce0+ 265 SystemDictionary::resolve_preloaded_classes
#    V  [jvm.dll+00ce4cc3] = 00ce3ac0+ 515 SystemDictionary::initialize
#    V  [jvm.dll+00d5669b] = 00d55370+ 811 Universe::genesis
#    V  [jvm.dll+00d595e8] = 00d585c0+  40 universe2_init
#    V  [jvm.dll+0074c7ec] = 0074b720+ 204 init_globals
#    V  [jvm.dll+00d2a534] = 00d28ef0+1604 Threads::create_vm
#    V  [jvm.dll+00830631] = 0082f560+ 209 JNI_CreateJavaVM_inner
#    V  [jvm.dll+00833eef] = 00832ed0+  31 JNI_CreateJavaVM
#    C  [jli.dll+0x52bf]
#    C  [ucrtbase.DLL+0x1d885]
#    C  [KERNEL32.DLL+0x13d2]
#    C  [ntdll.dll+0x154f4]
#
#
# NOTE: if the stack trace doesn't seem to have sense, try to modify hack_offset ...., or ask ioi.lam@oracle.com

set mapfile [lindex $argv 0]]
set dllname [file root [file tail $mapfile]].dll
puts $dllname
set fd [open [lindex $argv 0]]
set addrs {}

set hack_offset 0x1000

while {![eof $fd]} {
    set line [gets $fd]

    if {[regexp {Preferred load address is ([0-9a-f]+)} $line dummy p]} {
        puts "info: $dummy"
        set prefer 0x$p
    } elseif {[regexp -nocase -- {^ 0001:([0-9a-f]+) +(.*[.]obj)$} $line dummy addr name]} {
        if {[string comp "$addr" "00000000"] == 0} {            
            if {[regexp { ([0-9a-f]+) } $name dummy loaded]} {
                catch {
                    set hack_offset [expr 0x$loaded - $prefer]
                    puts "info: code offset = [format 0x%x $hack_offset] - $addr $name"
                }
            }
        }
        regsub {^((protected)|(public)|(private)): } $name "" name
        regsub {__thiscall } $name "" name
        regsub {__cdecl } $name "" name
        #puts $addr-$name
        set addr 0x$addr
        lappend addrs $addr
        set map($addr) $name
    }
}
close $fd

puts ----------------------------------------------------------------------

proc search {a} {
    global addrs

    set min 0
    set max [expr [llength $addrs] - 1]

    set k [lindex $addrs 0]

    while {$min < $max} {
        set n [expr ($min + $max) / 2]
        set k [lindex $addrs $n]
        #puts $min-$n-$max-$k->$a
        if {$a > $k} {
            if {$min == $n} {
                break
            }
            set min $n
        } elseif {$a == $k} {
            return $a
        } else {
            set max $n
        }
    }

    return $k
}

# https://en.wikiversity.org/wiki/Visual_C%2B%2B_name_mangling

set table {
    {"0" "Constructor"         "operator /=" }
    {"1" "Destructor"          "operator %=" }
    {"2" "operator new"        "operator >>=" }
    {"3" "operator delete"     "operator <<=" }
    {"4" "operator ="          "operator &=" }
    {"5" "operator >>"         "operator |=" }
    {"6" "operator <<"         "operator ^=" }
    {"7" "operator !"          "vftable" }
    {"8" "operator =="         "vbtable" }
    {"9" "operator !="         "vcall" }
    {"A" "operator[]"          "typeof" "managed vector constructor iterator"}
    {"B" "operator returntype" "local static guard" "managed vector destructor iterator"}
    {"C" "operator ->"         "String constant (see below)" "eh vector copy constructor iterator"}
    {"D" "operator *"          "vbase destructor" "eh vector vbase copy constructor iterator"}
    {"E" "operator ++"         "vector deleting destructor" "dynamic initializer (Used by CRT entry point to construct non-trivial? global objects)"}
    {"F" "operator --"         "default constructor closure" "dynamic atexit destructor` (Used by CRT to destroy non-trivial? global objects on program exit)"}
    {"G" "operator -"          "scalar deleting destructor" "vector copy constructor iterator"}
    {"H" "operator +"          "vector constructor iterator" "vector vbase copy constructor iterator"}
    {"I" "operator &"          "vector destructor iterator" "managed vector copy constructor iterator"}
    {"J" "operator ->*"        "vector vbase constructor iterator" "local static thread guard"}
    {"K" "operator /"          "virtual displacement map" "user-defined literal operator"}
    {"L" "operator %"          "eh vector constructor iterator" }
    {"M" "operator <"          "eh vector destructor iterator" }
    {"N" "operator <="         "eh vector vbase constructor iterator" }
    {"O" "operator >"          "copy constructor closure" }
    {"P" "operator >="         "udt returning (prefix)" }
    {"Q" "operator,"           "Unknown" }
    {"R" "operator ()"         "RTTI-related code (see below)" }
    {"S" "operator ~"          "local vftable" }
    {"T" "operator ^"          "local vftable constructor closure" }
    {"U" "operator |"          "operator new[]" }
    {"V" "operator &&"         "operator delete[]" }
    {"W" "operator ||"         "omni callsig" }
    {"X" "operator *="         "placement delete closure" }
    {"Y" "operator +="         "placement delete[] closure" }
    {"Z" "operator -="          }
}


proc demangle {s} {
    if {[regexp {^[?]([^@]+)@([^@]+)@@([^ ]*)} $s dummy a b tail]} {
       #return "${b}::${a} $tail"
        return "${b}::${a}"
    }
    if {[regexp {^[?]([^@]+)@@([^ ]*)} $s dummy a tail]} {
        if {[regexp {^[?](.)(.*)} $a dummy code name]} {
            if {$code == 0} {
                set a ${name}::$name
            } elseif {$code == 1} {
                set a ${name}::~$name
            }
        }

       #return "${a} $tail"
        return "${a}"
    }
    if {[regexp {^[@0-9A-Za-z_]+} $s csymbol]} {
        return $csymbol
    }
    return $s
}

set addrs [lsort $addrs]

set fd [open [lindex $argv 1]]
while {![eof $fd]} {
    set line [gets $fd]
    if {[regexp "(.*${dllname}\[+\])(0x\[0-9a-f\]+)" $line dummy head addr]} {
        # Hack, jvm.map says:  Preferred load address is 08000000, but 0001:0000 is loaded at 08001000
        # so we subtract 0x1000 for each adress
        set base [search [expr $addr - $hack_offset]]
        puts [format {%s%08x] = %08x+%4d %s} \
            $head $addr $base [expr $addr - $hack_offset - $base] [demangle $map($base)]]
    } else {
        puts $line
    }
}
