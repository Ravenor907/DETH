package org.diasurgical.devilutionx;

import net.lingala.zip4j.ZipFile;
import net.lingala.zip4j.model.FileHeader;

import org.apache.commons.compress.PasswordRequiredException;
import org.apache.commons.compress.archivers.ArchiveEntry;
import org.apache.commons.compress.archivers.sevenz.SevenZArchiveEntry;
import org.apache.commons.compress.archivers.sevenz.SevenZFile;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import org.apache.commons.compress.compressors.xz.XZCompressorInputStream;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * DETH: finds game files inside archives and extracts them.
 *
 * Supported: .zip (plain, ZipCrypto and AES), .7z (plain and AES-256), .tar, .tar.gz/.tgz,
 * .tar.xz/.txz, .gz and .xz, also nested inside each other. Everything streams through small
 * buffers, so a 500 MB DIABDAT.MPQ never sits in memory. Formats that need random access (zip, 7z)
 * read from a file; archives found inside other archives are first copied to a temp file.
 *
 * Plain Java on purpose (no Android classes), so it runs in JVM unit tests.
 */
public class ArchiveImporter {

	public enum Kind {
		NONE, ZIP, SEVEN_Z, TAR, GZ, XZ, RAR, EXE
	}

	/** Decides which files inside an archive are wanted (by file name, without folders). */
	public interface NameFilter {
		boolean accept(String fileName);
	}

	/** Asks the player for a password. Returns null to skip the archive. */
	public interface PasswordCallback {
		String requestPassword(String archiveName, boolean wrongPassword);
	}

	public static final class Result {
		/** Game files written to the destination. */
		public final List<String> extracted = new ArrayList<>();
		/** Archives skipped because the password prompt was cancelled. */
		public final List<String> skipped = new ArrayList<>();
		/** RAR archives found (not supported on Android). */
		public final List<String> rarArchives = new ArrayList<>();
		/** Installers (.exe) picked directly (not supported on Android). */
		public final List<String> installers = new ArrayList<>();
		/** Problems, as "archive name: message". */
		public final List<String> errors = new ArrayList<>();
	}

	/** Archives inside archives are followed this many levels deep. */
	public static final int MAX_DEPTH = 6;
	/** Upper bound for LZMA/XZ dictionaries; a wrong 7z password can produce absurd values. */
	private static final int MEMORY_LIMIT_KB = 256 * 1024;
	private static final int BUFFER_SIZE = 1 << 16;
	private static final int MAGIC_SIZE = 512;
	private static final String TEMP_PREFIX = "deth-import-";
	private static final String NEEDS_NEWER_ANDROID = "this archive type needs Android 8.0 or newer";

	private final File destinationDir;
	private final File tempDir;
	private final NameFilter filter;
	private final PasswordCallback passwordCallback;
	private boolean javaNioAvailable = true;
	private Result result;

	/**
	 * @param destinationDir where game files go (written as "name~", then renamed)
	 * @param tempDir scratch space for archives that need random access, e.g. Context.getCacheDir()
	 * @param filter which file names to extract
	 * @param passwordCallback asked for passwords; may be null (encrypted archives are then skipped)
	 */
	public ArchiveImporter(File destinationDir, File tempDir, NameFilter filter, PasswordCallback passwordCallback) {
		this.destinationDir = destinationDir;
		this.tempDir = tempDir;
		this.filter = filter;
		this.passwordCallback = passwordCallback;
	}

	/** Imports from an archive file on disk. The name decides the format (falls back to the file's content). */
	public Result importFile(File archive, String name) {
		result = new Result();
		Kind kind = kindForName(name);
		if (kind == Kind.NONE) {
			try (InputStream in = new FileInputStream(archive)) {
				kind = detect(name, peek(new BufferedInputStream(in, MAGIC_SIZE), MAGIC_SIZE));
			} catch (IOException e) {
				addError(name, e);
				return result;
			}
		}
		if (kind == Kind.EXE) {
			addOnce(result.installers, name);
			return result;
		}
		processFile(archive, name, kind, 0);
		return result;
	}

	/**
	 * Imports from a stream (e.g. a content:// Uri). Zip and 7z are copied to the temp dir first
	 * and the copy is deleted afterwards. The caller closes the stream.
	 */
	public Result importStream(InputStream input, String name) {
		result = new Result();
		BufferedInputStream in = new BufferedInputStream(input, BUFFER_SIZE);
		try {
			Kind kind = detect(name, peek(in, MAGIC_SIZE));
			if (needsJavaNio(kind) && !javaNioAvailable) {
				addOnce(result.errors, name + ": " + NEEDS_NEWER_ANDROID);
				return result;
			}
			switch (kind) {
			case ZIP:
			case SEVEN_Z: {
				File temp = spill(in);
				try {
					processFile(temp, name, kind, 0);
				} finally {
					deleteQuietly(temp);
				}
				break;
			}
			case TAR:
				processTar(in, 0);
				break;
			case GZ:
			case XZ:
				processCompressed(in, kind, name, 0);
				break;
			case RAR:
				addOnce(result.rarArchives, name);
				break;
			case EXE:
				addOnce(result.installers, name);
				break;
			default:
				String base = baseName(name);
				if (filter.accept(base))
					writeGameFile(in, base);
				break;
			}
		} catch (DestinationException e) {
			addError(name, e.getCause());
		} catch (IOException | RuntimeException e) {
			addError(name, e);
		}
		return result;
	}

	/**
	 * Android before 8.0 has no java.nio.file, which commons-compress needs. With false, 7z, tar,
	 * gz and xz (also inside a zip) are reported as errors instead of crashing.
	 */
	public void setJavaNioAvailable(boolean available) {
		javaNioAvailable = available;
	}

	/** Removes temp files left behind by an import that was killed half way. */
	public void cleanTempDir() {
		File[] leftovers = tempDir.listFiles((dir, fileName) -> fileName.startsWith(TEMP_PREFIX));
		if (leftovers == null)
			return;
		for (File file : leftovers)
			deleteQuietly(file);
	}

	/** Format by file name only. */
	public static Kind kindForName(String name) {
		if (name == null)
			return Kind.NONE;
		String lower = name.toLowerCase(Locale.ROOT);
		if (lower.endsWith(".zip"))
			return Kind.ZIP;
		if (lower.endsWith(".7z"))
			return Kind.SEVEN_Z;
		if (lower.endsWith(".tar"))
			return Kind.TAR;
		if (lower.endsWith(".gz") || lower.endsWith(".tgz"))
			return Kind.GZ;
		if (lower.endsWith(".xz") || lower.endsWith(".txz"))
			return Kind.XZ;
		if (lower.endsWith(".rar"))
			return Kind.RAR;
		if (lower.endsWith(".exe"))
			return Kind.EXE;
		return Kind.NONE;
	}

	/** Format by file name, or by the first bytes when the name does not tell. */
	public static Kind detect(String name, byte[] magic) {
		Kind kind = kindForName(name);
		if (kind != Kind.NONE || magic == null)
			return kind;
		if (startsWith(magic, 0, 'P', 'K', 3, 4) || startsWith(magic, 0, 'P', 'K', 5, 6))
			return Kind.ZIP;
		if (startsWith(magic, 0, '7', 'z', 0xBC, 0xAF, 0x27, 0x1C))
			return Kind.SEVEN_Z;
		if (startsWith(magic, 0, 0x1F, 0x8B))
			return Kind.GZ;
		if (startsWith(magic, 0, 0xFD, '7', 'z', 'X', 'Z', 0))
			return Kind.XZ;
		if (startsWith(magic, 0, 'R', 'a', 'r', '!'))
			return Kind.RAR;
		if (startsWith(magic, 257, 'u', 's', 't', 'a', 'r'))
			return Kind.TAR;
		if (startsWith(magic, 0, 'M', 'Z'))
			return Kind.EXE;
		return Kind.NONE;
	}

	/** True for the formats read with commons-compress, which needs java.nio.file (Android 8.0+). */
	public static boolean needsJavaNio(Kind kind) {
		return kind == Kind.SEVEN_Z || kind == Kind.TAR || kind == Kind.GZ || kind == Kind.XZ;
	}

	/** Reads up to count bytes without consuming them. */
	public static byte[] peek(BufferedInputStream in, int count) throws IOException {
		in.mark(count);
		byte[] magic = new byte[count];
		int total = 0;
		try {
			while (total < count) {
				int read = in.read(magic, total, count - total);
				if (read < 0)
					break;
				total += read;
			}
		} finally {
			in.reset();
		}
		if (total == count)
			return magic;
		byte[] shorter = new byte[total];
		System.arraycopy(magic, 0, shorter, 0, total);
		return shorter;
	}

	// ---- formats ----

	/** Handles one archive file; problems are recorded, never thrown. */
	private void processFile(File file, String name, Kind kind, int depth) {
		if (needsJavaNio(kind) && !javaNioAvailable) {
			addOnce(result.errors, name + ": " + NEEDS_NEWER_ANDROID);
			return;
		}
		try {
			switch (kind) {
			case ZIP:
				processZip(file, name, depth);
				break;
			case SEVEN_Z:
				process7z(file, name, depth);
				break;
			case TAR:
				try (InputStream in = new BufferedInputStream(new FileInputStream(file), BUFFER_SIZE)) {
					processTar(in, depth);
				}
				break;
			case GZ:
			case XZ:
				try (InputStream in = new BufferedInputStream(new FileInputStream(file), BUFFER_SIZE)) {
					processCompressed(in, kind, name, depth);
				}
				break;
			case RAR:
				addOnce(result.rarArchives, name);
				break;
			case EXE:
				break; // an installer inside an archive (e.g. next to DIABDAT.MPQ on a CD copy): ignore
			default: {
				String base = baseName(name);
				if (filter.accept(base)) {
					try (InputStream in = new FileInputStream(file)) {
						writeGameFile(in, base);
					}
				}
				break;
			}
			}
		} catch (DestinationException e) {
			addError(name, e.getCause());
		} catch (IOException | RuntimeException e) {
			addError(name, e);
		}
	}

	private void processZip(File file, String name, int depth) throws IOException {
		try (ZipFile zip = new ZipFile(file)) {
			if (!zip.isValidZipFile())
				throw new IOException("not a valid zip file");
			boolean encrypted = zip.isEncrypted();
			boolean wrongPassword = false;
			while (true) {
				if (encrypted) {
					String password = askPassword(name, wrongPassword);
					if (password == null) {
						addOnce(result.skipped, name);
						return;
					}
					zip.setPassword(password.toCharArray());
				}
				try {
					for (FileHeader header : zip.getFileHeaders()) {
						if (header.isDirectory() || !isWanted(header.getFileName()))
							continue;
						try (InputStream in = zip.getInputStream(header)) {
							handleEntry(in, header.getFileName(), depth);
						}
					}
					return;
				} catch (DestinationException e) {
					throw e;
				} catch (IOException e) {
					// zip4j reports WRONG_PASSWORD up front; a ZipCrypto password that slips past
					// the 1-in-256 header check shows up as a broken stream or a checksum mismatch.
					if (!encrypted)
						throw e;
					wrongPassword = true;
				}
			}
		}
	}

	private void process7z(File file, String name, int depth) throws IOException {
		char[] password = null;
		boolean wrongPassword = false;
		while (true) {
			try {
				SevenZFile.Builder builder = SevenZFile.builder().setFile(file).setMaxMemoryLimitKiB(MEMORY_LIMIT_KB);
				if (password != null)
					builder.setPassword(password);
				try (SevenZFile sevenZ = builder.get()) {
					InputStream entryStream = new SevenZEntryStream(sevenZ);
					SevenZArchiveEntry entry;
					while ((entry = sevenZ.getNextEntry()) != null) {
						if (entry.isDirectory() || !entry.hasStream() || !isWanted(entry.getName()))
							continue;
						handleEntry(entryStream, entry.getName(), depth);
					}
				}
				return;
			} catch (PasswordRequiredException e) {
				wrongPassword = false;
			} catch (DestinationException e) {
				throw e;
			} catch (IOException | RuntimeException e) {
				// 7z cannot check a password; a wrong one turns into garbage that fails to decode
				// or fails the CRC check.
				if (password == null)
					throw e;
				wrongPassword = true;
			}
			String answer = askPassword(name, wrongPassword);
			if (answer == null) {
				addOnce(result.skipped, name);
				return;
			}
			password = answer.toCharArray();
		}
	}

	private void processTar(InputStream in, int depth) throws IOException {
		TarArchiveInputStream tar = new TarArchiveInputStream(in);
		ArchiveEntry entry;
		while ((entry = tar.getNextEntry()) != null) {
			if (entry instanceof TarArchiveEntry && !((TarArchiveEntry) entry).isFile())
				continue;
			if (entry.isDirectory() || !isWanted(entry.getName()))
				continue;
			handleEntry(tar, entry.getName(), depth);
		}
	}

	/** A .gz or .xz holds a single file: a tar, another archive, or a game file. */
	private void processCompressed(InputStream in, Kind kind, String name, int depth) throws IOException {
		if (depth > MAX_DEPTH)
			throw new IOException("archives are nested too deeply");
		InputStream decompressed = kind == Kind.GZ
		    ? new GzipCompressorInputStream(in, true)
		    : new XZCompressorInputStream(in, true, MEMORY_LIMIT_KB);
		try {
			String innerName = innerName(name);
			BufferedInputStream inner = new BufferedInputStream(decompressed, BUFFER_SIZE);
			Kind innerKind = detect(innerName, peek(inner, MAGIC_SIZE));
			switch (innerKind) {
			case TAR:
				processTar(inner, depth);
				break;
			case GZ:
			case XZ:
				processCompressed(inner, innerKind, innerName, depth + 1);
				break;
			case ZIP:
			case SEVEN_Z: {
				File temp = spill(inner);
				try {
					processFile(temp, innerName, innerKind, depth + 1);
				} finally {
					deleteQuietly(temp);
				}
				break;
			}
			case RAR:
				addOnce(result.rarArchives, innerName);
				break;
			case EXE:
				break;
			default: {
				String base = baseName(innerName);
				if (filter.accept(base))
					writeGameFile(inner, base);
				break;
			}
			}
		} finally {
			decompressed.close();
		}
	}

	/**
	 * One file inside an archive: a game file is written out, an archive is copied to a temp
	 * file and searched in turn. The stream is not closed here.
	 */
	private void handleEntry(InputStream in, String entryName, int depth) throws IOException {
		String base = baseName(entryName);
		Kind kind = kindForName(base);
		switch (kind) {
		case RAR:
			addOnce(result.rarArchives, base);
			return;
		case ZIP:
		case SEVEN_Z:
		case TAR:
		case GZ:
		case XZ: {
			if (depth + 1 > MAX_DEPTH) {
				addOnce(result.errors, base + ": archives are nested too deeply");
				return;
			}
			// Copying first keeps read errors of the outer archive (e.g. a wrong password)
			// apart from problems of the inner one, which are recorded under its own name.
			File temp = spill(in);
			try {
				processFile(temp, base, kind, depth + 1);
			} finally {
				deleteQuietly(temp);
			}
			return;
		}
		default:
			if (filter.accept(base))
				writeGameFile(in, base);
		}
	}

	// ---- helpers ----

	private boolean isWanted(String entryName) {
		String base = baseName(entryName);
		if (base.isEmpty())
			return false;
		Kind kind = kindForName(base);
		return filter.accept(base) || (kind != Kind.NONE && kind != Kind.EXE);
	}

	private String askPassword(String archiveName, boolean wrongPassword) {
		if (passwordCallback == null)
			return null;
		return passwordCallback.requestPassword(archiveName, wrongPassword);
	}

	/** Writes "name~" next to the target, then renames it, so a broken import never leaves half a file. */
	private void writeGameFile(InputStream in, String fileName) throws IOException {
		File target = new File(destinationDir, fileName);
		File temp = new File(destinationDir, fileName + "~");
		boolean done = false;
		try {
			copy(in, temp);
			if (!temp.renameTo(target)) {
				deleteQuietly(target);
				if (!temp.renameTo(target))
					throw new DestinationException(new IOException("could not rename " + temp.getName()));
			}
			done = true;
		} finally {
			if (!done)
				deleteQuietly(temp);
		}
		addOnce(result.extracted, fileName);
	}

	/** Copies the rest of the stream into a new temp file. */
	private File spill(InputStream in) throws IOException {
		File temp;
		try {
			if (!tempDir.isDirectory() && !tempDir.mkdirs())
				throw new IOException("no temp folder: " + tempDir);
			temp = File.createTempFile(TEMP_PREFIX, ".tmp", tempDir);
		} catch (IOException e) {
			throw new DestinationException(e);
		}
		boolean done = false;
		try {
			copy(in, temp);
			done = true;
			return temp;
		} finally {
			if (!done)
				deleteQuietly(temp);
		}
	}

	/** Read errors pass through as they are; write errors become DestinationException. */
	private static void copy(InputStream in, File target) throws IOException {
		OutputStream out;
		try {
			out = new FileOutputStream(target);
		} catch (IOException e) {
			throw new DestinationException(e);
		}
		try {
			byte[] buffer = new byte[BUFFER_SIZE];
			int read;
			while ((read = in.read(buffer)) != -1) {
				try {
					out.write(buffer, 0, read);
				} catch (IOException e) {
					throw new DestinationException(e);
				}
			}
		} finally {
			try {
				out.close();
			} catch (IOException e) {
				// The data may not be on disk; the caller treats this as a failed write.
				throw new DestinationException(e);
			}
		}
	}

	static String baseName(String path) {
		if (path == null)
			return "";
		int slash = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
		return slash >= 0 ? path.substring(slash + 1) : path;
	}

	/** "x.tar.gz" and "x.tgz" hold "x.tar"; "y.mpq.xz" holds "y.mpq". */
	static String innerName(String name) {
		String lower = name.toLowerCase(Locale.ROOT);
		if (lower.endsWith(".tgz") || lower.endsWith(".txz"))
			return name.substring(0, name.length() - 4) + ".tar";
		if (lower.endsWith(".gz") || lower.endsWith(".xz"))
			return name.substring(0, name.length() - 3);
		return name;
	}

	private static boolean startsWith(byte[] data, int offset, int... expected) {
		if (data.length < offset + expected.length)
			return false;
		for (int i = 0; i < expected.length; i++) {
			if ((data[offset + i] & 0xFF) != expected[i])
				return false;
		}
		return true;
	}

	private void addError(String name, Throwable error) {
		String message = error == null || error.getMessage() == null
		    ? (error == null ? "unknown error" : error.getClass().getSimpleName())
		    : error.getMessage();
		addOnce(result.errors, name + ": " + message);
	}

	private static void addOnce(List<String> list, String value) {
		if (!list.contains(value))
			list.add(value);
	}

	private static void deleteQuietly(File file) {
		if (file != null && file.exists() && !file.delete())
			file.deleteOnExit();
	}

	/** Failure writing to our own storage (disk full, no permission), as opposed to reading the archive. */
	static final class DestinationException extends IOException {
		DestinationException(IOException cause) {
			super(cause.getMessage(), cause);
		}
	}

	/** The current 7z entry as an InputStream (SevenZFile reads entry by entry). */
	private static final class SevenZEntryStream extends InputStream {
		private final SevenZFile sevenZ;

		SevenZEntryStream(SevenZFile sevenZ) {
			this.sevenZ = sevenZ;
		}

		@Override
		public int read() throws IOException {
			return sevenZ.read();
		}

		@Override
		public int read(byte[] buffer, int offset, int length) throws IOException {
			return sevenZ.read(buffer, offset, length);
		}

		@Override
		public void close() {
			// The SevenZFile is closed by its owner.
		}
	}
}
