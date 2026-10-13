package org.joltfx.mobile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Headless carrier for the production Activity JNI signatures. */
public final class JoltPlayerActivity {
    private native long nativeCreate(int width, int height, double duration);
    private native void nativeDestroy(long handle);
    private native int nativeEdit(long handle, String op, int a, int b, int c, double value, String text);
    private native String nativeScene3DState(long handle);
    private native int nativeProjectFile(long handle, String path, boolean save);
    private native int[] nativePixels(long handle);
    private native int nativeSeek(long handle, double seconds);
    private static void check(boolean condition) { if (!condition) throw new AssertionError(); }
    public static void run() throws Exception {
        JoltPlayerActivity activity = new JoltPlayerActivity();
        long handle = activity.nativeCreate(320, 180, 60);
        check(handle != 0);
        Path directory = Files.createTempDirectory("jfx-jni-");
        Path path = directory.resolve("🎨 رنگ.jfx");
        try {
            check(activity.nativeEdit(handle, "3d.add", 0, 0, 0, 0, "cube") == 0);
            check(activity.nativeEdit(handle, "3d.name", 0, 0, 0, 0, "🎨 رنگ") == 0);
            String source = "(import jolt.math (sin HALF-PI)) (import jolt.strings (regex-match?)) " +
                "(defkernel lift [time frame index value] " +
                "(if (regex-match? \"^🎨$\" \"🎨\") (+ value (* time (sin HALF-PI))) value))";
            check(activity.nativeEdit(handle, "3d.script", 0, 1, 0, 0, source) == 0);
            String before = activity.nativeScene3DState(handle);
            check(before.contains("🎨 رنگ") && before.contains("HALF-PI"));
            int[] first = activity.nativePixels(handle);
            check(first != null && first.length == 320 * 180);
            check(activity.nativeSeek(handle, 1) == 0);
            int[] next = activity.nativePixels(handle);
            check(!Arrays.equals(first, next));
            check(activity.nativeProjectFile(handle, path.toString(), true) == 0);
            byte[] bytes = Files.readAllBytes(path);
            check(new String(bytes, StandardCharsets.UTF_8).contains("🎨 رنگ"));
            check(activity.nativeProjectFile(handle, path.toString(), false) == 0);
            check(activity.nativeSeek(handle, 1) == 0);
            check(Arrays.equals(next, activity.nativePixels(handle)));
            // A failed serialization must not truncate an existing file.
            check(activity.nativeProjectFile(0, path.toString(), true) != 0);
            check(Arrays.equals(bytes, Files.readAllBytes(path)));
            String state = activity.nativeScene3DState(handle);
            Files.writeString(path, "scene3d invalid\n");
            check(activity.nativeProjectFile(handle, path.toString(), false) != 0);
            check(activity.nativeScene3DState(handle).equals(state));
            try { activity.nativeEdit(handle, "3d.name", 0, 0, 0, 0, "bad\u0000name"); throw new AssertionError(); }
            catch (IllegalArgumentException expected) { }
            try { activity.nativeEdit(handle, "3d.name", 0, 0, 0, 0, "bad\ud800name"); throw new AssertionError(); }
            catch (IllegalArgumentException expected) { }
            check(activity.nativeScene3DState(handle).equals(state));
            System.out.println("Android JNI: Unicode, typed animation, pixels, persistence and failure atomicity passed");
        } finally {
            activity.nativeDestroy(handle);
            try (var files = Files.list(directory)) { for (Path file : files.toList()) Files.delete(file); }
            Files.delete(directory);
        }
    }
}
