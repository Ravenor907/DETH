package org.diasurgical.devilutionx;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Objects;

public class ExternalFilesManager {
	private String externalFilesDirectory;
	private final Context context;

	public ExternalFilesManager(Context context) {
		this.context = context.getApplicationContext();
		externalFilesDirectory = chooseExternalFilesDirectory(context);
	}

	public String getExternalFilesDirectory() {
		return externalFilesDirectory;
	}

	public boolean hasFile(String fileName) {
		File file = getFile(fileName);
		return file.exists();
	}

	/**
	 * DETH: the free shareware data ships inside the APK. Copy it out once so the
	 * game can open it as a regular file. Returns true if spawn.mpq is available.
	 */
	public boolean ensureBundledShareware() {
		if (hasFile("spawn.mpq"))
			return true;
		File target = getFile("spawn.mpq");
		File temp = getFile("spawn.mpq~");
		try (java.io.InputStream in = context.getAssets().open("bundled/spawn.mpq");
		     java.io.OutputStream out = new java.io.FileOutputStream(temp)) {
			byte[] buffer = new byte[1 << 16];
			int read;
			while ((read = in.read(buffer)) > 0)
				out.write(buffer, 0, read);
		} catch (java.io.IOException e) {
			temp.delete();
			return false; // not bundled in this build
		}
		return temp.renameTo(target);
	}

	public long fileSize(String fileName) {
		File file = getFile(fileName);
		return file.length();
	}

	public File getFile(String fileName) {
		return new File(externalFilesDirectory + "/" + fileName);
	}

	public void migrateFile(File file) {
		File newPath = new File(externalFilesDirectory + "/" + file.getName());

		if (newPath.exists()) {
			if (file.canWrite()) {
				//noinspection ResultOfMethodCallIgnored
				file.delete();
			}
			return;
		}
		if (!file.renameTo(newPath)) {
			if (copyFile(file, newPath) && file.canWrite()) {
				//noinspection ResultOfMethodCallIgnored
				file.delete();
			}
		}
	}

	private String chooseExternalFilesDirectory(Context context) {
		if (Build.VERSION.SDK_INT >= 19) {
			File[] externalDirs = context.getExternalFilesDirs(null);

			for (File dir : externalDirs) {
				if (dir == null) {
					continue;
				}
				File[] iniFiles = dir.listFiles((dir1, name) -> name.equals("diablo.ini"));
				if (iniFiles != null && iniFiles.length > 0) {
					return dir.getAbsolutePath();
				}
			}

			for (File dir : externalDirs) {
				if (dir == null) {
					continue;
				}
				File[] lastFiles = dir.listFiles();
				if (lastFiles != null && lastFiles.length > 0) {
					return dir.getAbsolutePath();
				}
			}
		}

		// Fallback to the primary external storage directory
		return context.getExternalFilesDir(null).getAbsolutePath();
	}

	private boolean copyFile(File src, File dst) {
		try {
			InputStream in = new FileInputStream(src);
			try {
				OutputStream out = new FileOutputStream(dst);
				try {
					// Transfer bytes from in to out
					byte[] buf = new byte[1024];
					int len;
					while ((len = in.read(buf)) > 0) {
						out.write(buf, 0, len);
					}
				} finally {
					out.close();
				}
			} finally {
				in.close();
			}
		} catch (IOException exception) {
			Log.e("copyFile", Objects.requireNonNull(exception.getMessage()));
			if (dst.exists()) {
				//noinspection ResultOfMethodCallIgnored
				dst.delete();
			}
			return false;
		}

		return true;
	}
}
