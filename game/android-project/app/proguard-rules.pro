# Add project specific ProGuard rules here.
# By default, the flags in this file are appended to flags specified
# in [sdk]/tools/proguard/proguard-android.txt
# You can edit the include path and order by changing the proguardFiles
# directive in build.gradle.
#
# For more details, see
#   http://developer.android.com/guide/developing/tools/proguard.html

# Add any project specific keep options here:

# If your project uses WebView with JS, uncomment the following
# and specify the fully qualified class name to the JavaScript interface
# class:
#-keepclassmembers class fqcn.of.javascript.interface.for.webview {
#   public *;
#}

# DETH: archive import (ArchiveImporter). commons-compress has optional codecs that are not
# bundled (Brotli, Zstandard, ASM for pack200, OSGi); none are used.
-dontwarn org.brotli.dec.**
-dontwarn com.github.luben.zstd.**
-dontwarn org.objectweb.asm.**
-dontwarn org.osgi.**
-keep class org.apache.commons.compress.archivers.sevenz.** { *; }
-keep class org.apache.commons.compress.compressors.xz.** { *; }
-keep class org.apache.commons.compress.compressors.gzip.** { *; }
-keep class org.apache.commons.compress.archivers.tar.** { *; }
-keep class org.tukaani.xz.** { *; }
-keep class net.lingala.zip4j.** { *; }
