package org.diasurgical.devilutionx;

import net.lingala.zip4j.ZipFile;
import net.lingala.zip4j.model.ZipParameters;
import net.lingala.zip4j.model.enums.AesKeyStrength;
import net.lingala.zip4j.model.enums.EncryptionMethod;

import org.apache.commons.compress.archivers.sevenz.SevenZArchiveEntry;
import org.apache.commons.compress.archivers.sevenz.SevenZOutputFile;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorOutputStream;
import org.apache.commons.compress.compressors.xz.XZCompressorOutputStream;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Deque;
import java.util.List;
import java.util.Locale;
import java.util.Random;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

/**
 * Builds every fixture at test time. The encrypted 7z cannot be written by commons-compress, so it
 * comes from CI (7zz) through DETH_FIXTURE_7Z_AES or -Ddeth.fixture.7zAes; without it that test is skipped.
 */
public class ArchiveImporterTest {

	private static final String PASSWORD = "deth-test";
	private static final String FIXTURE_7Z_CONTENT = "deth-7z-aes-fixture";

	@Rule
	public TemporaryFolder folder = new TemporaryFolder();

	private File work;
	private File dest;
	private File temp;
	private File mpq;
	private byte[] mpqBytes;

	/** Hands out scripted answers and records whether each prompt said "wrong password". */
	private static final class ScriptedPasswords implements ArchiveImporter.PasswordCallback {
		final Deque<String> answers = new ArrayDeque<>();
		final List<Boolean> wrongFlags = new ArrayList<>();

		ScriptedPasswords(String... answers) {
			this.answers.addAll(Arrays.asList(answers));
		}

		@Override
		public String requestPassword(String archiveName, boolean wrongPassword) {
			wrongFlags.add(wrongPassword);
			return answers.poll(); // null once the script runs out: the player pressed Cancel
		}
	}

	@Before
	public void setUp() throws IOException {
		work = folder.newFolder("work");
		dest = folder.newFolder("dest");
		temp = folder.newFolder("cache");
		mpqBytes = new byte[300 * 1024 + 17];
		new Random(907).nextBytes(mpqBytes);
		mpq = new File(work, "DIABDAT.MPQ");
		Files.write(mpq.toPath(), mpqBytes);
	}

	private ArchiveImporter importer(ArchiveImporter.PasswordCallback passwords) {
		return new ArchiveImporter(dest, temp, name -> name.toLowerCase(Locale.ROOT).endsWith(".mpq"), passwords);
	}

	private void assertImported(ArchiveImporter.Result result) throws IOException {
		assertEquals("errors: " + result.errors, Collections.emptyList(), result.errors);
		assertEquals(Collections.singletonList("DIABDAT.MPQ"), result.extracted);
		assertArrayEquals(mpqBytes, Files.readAllBytes(new File(dest, "DIABDAT.MPQ").toPath()));
		assertClean();
	}

	private void assertNothingImported() {
		assertTrue(result(dest).isEmpty());
		assertClean();
	}

	/** No temp copies and no half-written "name~" files are left behind. */
	private void assertClean() {
		assertEquals(Collections.emptyList(), result(temp));
		for (String name : result(dest))
			assertFalse(name, name.endsWith("~"));
	}

	private static List<String> result(File dir) {
		String[] names = dir.list();
		return names == null ? Collections.<String>emptyList() : Arrays.asList(names);
	}

	// ---- fixture builders ----

	private File zip(String name, File content, String entryName, EncryptionMethod encryption) throws IOException {
		File file = new File(work, name);
		ZipParameters parameters = new ZipParameters();
		parameters.setFileNameInZip(entryName);
		if (encryption != null) {
			parameters.setEncryptFiles(true);
			parameters.setEncryptionMethod(encryption);
			if (encryption == EncryptionMethod.AES)
				parameters.setAesKeyStrength(AesKeyStrength.KEY_STRENGTH_256);
		}
		try (ZipFile zip = encryption == null ? new ZipFile(file) : new ZipFile(file, PASSWORD.toCharArray())) {
			zip.addFile(content, parameters);
			File readme = new File(work, "readme.txt");
			Files.write(readme.toPath(), "not a game file".getBytes(StandardCharsets.UTF_8));
			ZipParameters readmeParameters = new ZipParameters(parameters);
			readmeParameters.setFileNameInZip("Diablo/readme.txt");
			zip.addFile(readme, readmeParameters);
		}
		return file;
	}

	private File sevenZ(String name, File content, String entryName) throws IOException {
		File file = new File(work, name);
		try (SevenZOutputFile out = new SevenZOutputFile(file)) {
			SevenZArchiveEntry entry = out.createArchiveEntry(content, entryName);
			out.putArchiveEntry(entry);
			out.write(Files.readAllBytes(content.toPath()));
			out.closeArchiveEntry();
		}
		return file;
	}

	private File tar(String name, File content, String entryName, boolean xz) throws IOException {
		File file = new File(work, name);
		try (OutputStream raw = new FileOutputStream(file);
		     OutputStream compressed = xz ? new XZCompressorOutputStream(raw) : new GzipCompressorOutputStream(raw);
		     TarArchiveOutputStream tar = new TarArchiveOutputStream(compressed)) {
			tar.putArchiveEntry(new TarArchiveEntry(content, entryName));
			Files.copy(content.toPath(), tar);
			tar.closeArchiveEntry();
			tar.finish();
		}
		return file;
	}

	// ---- zip ----

	@Test
	public void plainZipExtractsOnlyGameFiles() throws IOException {
		File zip = zip("game.zip", mpq, "Diablo/DIABDAT.MPQ", null);
		ScriptedPasswords passwords = new ScriptedPasswords();
		assertImported(importer(passwords).importFile(zip, zip.getName()));
		assertFalse(new File(dest, "readme.txt").exists());
		assertTrue(passwords.wrongFlags.isEmpty());
	}

	@Test
	public void plainZipFromStream() throws IOException {
		File zip = zip("game.zip", mpq, "DIABDAT.MPQ", null);
		try (InputStream in = new FileInputStream(zip)) {
			assertImported(importer(null).importStream(in, "game.zip"));
		}
	}

	@Test
	public void aesZipCorrectPassword() throws IOException {
		checkEncryptedZip(EncryptionMethod.AES);
	}

	@Test
	public void zipCryptoCorrectPassword() throws IOException {
		checkEncryptedZip(EncryptionMethod.ZIP_STANDARD);
	}

	private void checkEncryptedZip(EncryptionMethod method) throws IOException {
		File zip = zip("locked.zip", mpq, "Diablo/DIABDAT.MPQ", method);
		ScriptedPasswords passwords = new ScriptedPasswords(PASSWORD);
		assertImported(importer(passwords).importFile(zip, zip.getName()));
		assertEquals(Collections.singletonList(false), passwords.wrongFlags);
	}

	@Test
	public void aesZipWrongPasswordAsksAgain() throws IOException {
		checkWrongPassword(EncryptionMethod.AES);
	}

	@Test
	public void zipCryptoWrongPasswordAsksAgain() throws IOException {
		checkWrongPassword(EncryptionMethod.ZIP_STANDARD);
	}

	private void checkWrongPassword(EncryptionMethod method) throws IOException {
		File zip = zip("locked.zip", mpq, "Diablo/DIABDAT.MPQ", method);
		ScriptedPasswords passwords = new ScriptedPasswords("nope", "still-wrong", PASSWORD);
		assertImported(importer(passwords).importFile(zip, zip.getName()));
		assertEquals(Arrays.asList(false, true, true), passwords.wrongFlags);
	}

	@Test
	public void aesZipCancelSkips() throws IOException {
		checkCancel(EncryptionMethod.AES);
	}

	@Test
	public void zipCryptoCancelSkips() throws IOException {
		checkCancel(EncryptionMethod.ZIP_STANDARD);
	}

	private void checkCancel(EncryptionMethod method) throws IOException {
		File zip = zip("locked.zip", mpq, "Diablo/DIABDAT.MPQ", method);
		ScriptedPasswords passwords = new ScriptedPasswords("wrong");
		ArchiveImporter.Result result = importer(passwords).importFile(zip, zip.getName());
		assertEquals(Collections.singletonList("locked.zip"), result.skipped);
		assertTrue(result.extracted.isEmpty());
		assertEquals(Arrays.asList(false, true), passwords.wrongFlags);
		assertNothingImported();
	}

	// ---- 7z, tar ----

	@Test
	public void plainSevenZ() throws IOException {
		File sevenZ = sevenZ("game.7z", mpq, "Diablo/DIABDAT.MPQ");
		assertImported(importer(null).importFile(sevenZ, sevenZ.getName()));
	}

	@Test
	public void sevenZFromStreamUsesAndDeletesTempCopy() throws IOException {
		File sevenZ = sevenZ("game.7z", mpq, "Diablo/DIABDAT.MPQ");
		try (InputStream in = new FileInputStream(sevenZ)) {
			assertImported(importer(null).importStream(in, "game.7z"));
		}
	}

	@Test
	public void tarGz() throws IOException {
		File tgz = tar("game.tar.gz", mpq, "Diablo/DIABDAT.MPQ", false);
		try (InputStream in = new FileInputStream(tgz)) {
			assertImported(importer(null).importStream(in, "game.tar.gz"));
		}
	}

	@Test
	public void tarXz() throws IOException {
		File txz = tar("game.tar.xz", mpq, "Diablo/DIABDAT.MPQ", true);
		assertImported(importer(null).importFile(txz, txz.getName()));
	}

	@Test
	public void unknownNameIsDetectedByContent() throws IOException {
		File tgz = tar("download", mpq, "DIABDAT.MPQ", false);
		try (InputStream in = new FileInputStream(tgz)) {
			assertImported(importer(null).importStream(in, "download"));
		}
	}

	// ---- nesting ----

	@Test
	public void nestedSevenZipZipMpq() throws IOException {
		File innerZip = zip("inner.zip", mpq, "data/DIABDAT.MPQ", null);
		File outer = sevenZ("outer.7z", innerZip, "backup/inner.zip");
		assertImported(importer(null).importFile(outer, outer.getName()));
	}

	@Test
	public void nestedThreeLevelsWithPassword() throws IOException {
		File innerZip = zip("inner.zip", mpq, "data/DIABDAT.MPQ", EncryptionMethod.AES);
		File middle = sevenZ("middle.7z", innerZip, "inner.zip");
		File outer = tar("outer.tar.xz", middle, "stuff/middle.7z", true);
		ScriptedPasswords passwords = new ScriptedPasswords("wrong", PASSWORD);
		assertImported(importer(passwords).importFile(outer, outer.getName()));
		assertEquals(Arrays.asList(false, true), passwords.wrongFlags);
	}

	@Test
	public void cancelledInnerArchiveDoesNotStopOuter() throws IOException {
		File lockedZip = zip("locked.zip", mpq, "DIABDAT.MPQ", EncryptionMethod.ZIP_STANDARD);
		File hellfire = new File(work, "hellfire.mpq");
		Files.write(hellfire.toPath(), new byte[] { 1, 2, 3 });
		File outer = new File(work, "outer.zip");
		try (ZipFile zip = new ZipFile(outer)) {
			ZipParameters parameters = new ZipParameters();
			parameters.setFileNameInZip("locked.zip");
			zip.addFile(lockedZip, parameters);
			parameters.setFileNameInZip("hellfire.mpq");
			zip.addFile(hellfire, parameters);
		}
		ArchiveImporter.Result result = importer(new ScriptedPasswords()).importFile(outer, outer.getName());
		assertEquals(Collections.singletonList("locked.zip"), result.skipped);
		assertEquals(Collections.singletonList("hellfire.mpq"), result.extracted);
		assertClean();
	}

	// ---- unsupported ----

	@Test
	public void rarAndExeAreReported() throws IOException {
		File rar = new File(work, "game.rar");
		Files.write(rar.toPath(), "Rar!\u001a\u0007\u0000".getBytes(StandardCharsets.ISO_8859_1));
		File exe = new File(work, "setup_diablo.exe");
		Files.write(exe.toPath(), "MZ".getBytes(StandardCharsets.ISO_8859_1));
		assertEquals(Collections.singletonList("game.rar"), importer(null).importFile(rar, rar.getName()).rarArchives);
		assertEquals(Collections.singletonList("setup_diablo.exe"), importer(null).importFile(exe, exe.getName()).installers);
		assertEquals(ArchiveImporter.Kind.RAR, ArchiveImporter.detect("x", Files.readAllBytes(rar.toPath())));
		assertNothingImported();
	}

	// ---- encrypted 7z (fixture from CI) ----

	private File encryptedSevenZFixture() {
		String path = System.getProperty("deth.fixture.7zAes");
		if (path == null || path.isEmpty())
			path = System.getenv("DETH_FIXTURE_7Z_AES");
		Assume.assumeTrue("DETH_FIXTURE_7Z_AES not set", path != null && !path.isEmpty());
		File file = new File(path);
		Assume.assumeTrue("fixture missing: " + path, file.isFile());
		return file;
	}

	private void assertFixtureImported(ArchiveImporter.Result result) throws IOException {
		assertEquals("errors: " + result.errors, Collections.emptyList(), result.errors);
		assertEquals(Collections.singletonList("DIABDAT.MPQ"), result.extracted);
		String content = new String(Files.readAllBytes(new File(dest, "DIABDAT.MPQ").toPath()), StandardCharsets.UTF_8);
		assertEquals(FIXTURE_7Z_CONTENT, content.trim());
		assertClean();
	}

	@Test
	public void encryptedSevenZCorrectPassword() throws IOException {
		File fixture = encryptedSevenZFixture();
		ScriptedPasswords passwords = new ScriptedPasswords(PASSWORD);
		assertFixtureImported(importer(passwords).importFile(fixture, "fixture.7z"));
		assertEquals(Collections.singletonList(false), passwords.wrongFlags);
	}

	@Test
	public void encryptedSevenZWrongPasswordAsksAgain() throws IOException {
		File fixture = encryptedSevenZFixture();
		ScriptedPasswords passwords = new ScriptedPasswords("nope", PASSWORD);
		try (InputStream in = new FileInputStream(fixture)) {
			assertFixtureImported(importer(passwords).importStream(in, "fixture.7z"));
		}
		assertEquals(Arrays.asList(false, true), passwords.wrongFlags);
	}

	@Test
	public void encryptedSevenZCancelSkips() throws IOException {
		File fixture = encryptedSevenZFixture();
		ScriptedPasswords passwords = new ScriptedPasswords();
		ArchiveImporter.Result result = importer(passwords).importFile(fixture, "fixture.7z");
		assertEquals(Collections.singletonList("fixture.7z"), result.skipped);
		assertNothingImported();
	}
}
