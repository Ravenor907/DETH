package org.diasurgical.devilutionx;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.support.annotation.Nullable;
import android.os.Bundle;
import android.support.annotation.RequiresApi;
import android.support.v4.provider.DocumentFile;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.method.LinkMovementMethod;
import android.text.style.URLSpan;
import android.text.InputType;
import android.util.Log;
import android.util.TypedValue;
import android.view.View;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.BufferedInputStream;
import java.util.ArrayList;
import java.util.concurrent.CountDownLatch;

public class ImportActivity extends Activity {

	private static final int IMPORT_REQUEST_CODE = 0xD1AB70;
	private static final String TV_STUB_PACKAGE = "com.android.tv.frameworkpackagestubs";
	private static final String RECOMMENDED_FILE_BROWSER = "AnExplorer";
	private static final String RECOMMENDED_FILE_BROWSER_PACKAGE = "dev.dworks.apps.anexplorer";
	private boolean userConfirmed = false;
	private boolean importing = false;

	@RequiresApi(api = Build.VERSION_CODES.KITKAT)
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		if (findFileBrowser()) {
			runFileBrowser();
		} else {
			recommendFileBrowser();
		}
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
		if (requestCode != IMPORT_REQUEST_CODE)
			return;

		if (resultCode == Activity.RESULT_OK && data != null) {
			ArrayList<Uri> uriList = getItemUris(data.getClipData());

			Uri dataUri = data.getData();
			if (dataUri != null)
				uriList.add(dataUri);

			ArrayList<String> fileNames = getFileNames(uriList);
			ArrayList<String> overwrittenFiles = getOverwrittenFiles(fileNames);
			if (overwrittenFiles.isEmpty()) {
				startImport(uriList);
				return;
			}

			String overwrittenFileList = String.join("\n", overwrittenFiles);
			AlertDialog.Builder builder = new AlertDialog.Builder(this);
			builder.setMessage(getString(R.string.overwrite_query, overwrittenFileList));
			builder.setPositiveButton(R.string.continue_button, (dialog, which) -> { startImport(uriList); });
			builder.setNegativeButton(R.string.cancel_button, null);
			builder.setOnDismissListener(dialog -> {
				if (!importing)
					finish();
			});

			AlertDialog dialog = builder.create();
			dialog.show();
		} else {
			super.onBackPressed();
		}
	}

	private boolean findFileBrowser() {
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
		intent.addCategory(Intent.CATEGORY_OPENABLE);
		intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
		intent.setType("*/*");

		PackageManager packageManager = getPackageManager();
		ComponentName defaultPackage = intent.resolveActivity(packageManager);
		return defaultPackage != null && !TV_STUB_PACKAGE.equals(defaultPackage.getPackageName());
	}

	private void recommendFileBrowser()
	{
		String noFileBrowserText = getString(R.string.no_file_browser);
		SpannableString message;

		boolean isTelevision = getPackageManager().hasSystemFeature(PackageManager.FEATURE_LEANBACK);
		if (isTelevision) {
			String recommendation = getString(R.string.recommended_file_browser, RECOMMENDED_FILE_BROWSER);
			String messageText = noFileBrowserText + " " + recommendation;
			message = new SpannableString(messageText);

			int index = messageText.indexOf(RECOMMENDED_FILE_BROWSER);
			if (index >= 0) {
				String url = "market://details?id=" + RECOMMENDED_FILE_BROWSER_PACKAGE;
				URLSpan urlSpan = new URLSpan(url);
				message.setSpan(urlSpan, index, index + RECOMMENDED_FILE_BROWSER.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
			}
		} else {
			message = new SpannableString(noFileBrowserText);
		}

		AlertDialog.Builder builder = new AlertDialog.Builder(this);
		builder.setMessage(message);
		builder.setPositiveButton("OK", null);
		builder.setOnDismissListener(dialog -> {
			if (findFileBrowser()) {
				runFileBrowser();
			} else if (!isFinishing()) {
				finish();
			}
		});

		AlertDialog dialog = builder.create();
		dialog.show();

		View view = dialog.findViewById(android.R.id.message);
		if (view instanceof TextView) {
			TextView textView = (TextView)view;
			textView.setMovementMethod(LinkMovementMethod.getInstance());
		}
	}

	private void runFileBrowser() {
		ExternalFilesManager fileManager = new ExternalFilesManager(this);
		String externalFilesDir =  fileManager.getExternalFilesDirectory();

		AlertDialog.Builder builder = new AlertDialog.Builder(this);
		builder.setMessage(getString(R.string.import_data_info, externalFilesDir));
		builder.setPositiveButton(R.string.ok_button, (dialog, which) -> {
			userConfirmed = true;
			Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
			intent.addCategory(Intent.CATEGORY_OPENABLE);
			intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
			intent.setType("*/*");
			startActivityForResult(intent, IMPORT_REQUEST_CODE);
		});
		builder.setOnDismissListener(dialog -> {
			if (!userConfirmed && !isFinishing()) finish();
		});

		AlertDialog dialog = builder.create();
		dialog.show();
	}

	private ArrayList<Uri> getItemUris(ClipData clipData) {
		ArrayList<Uri> uriList = new ArrayList<>();
		if (clipData == null)
			return uriList;

		for (int i = 0; i < clipData.getItemCount(); i++) {
			ClipData.Item item = clipData.getItemAt(i);
			if (item == null)
				continue;

			Uri itemUri = item.getUri();
			if (itemUri == null)
				continue;

			uriList.add(itemUri);
		}

		return uriList;
	}

	private ArrayList<String> getFileNames(ArrayList<Uri> uriList) {
		ArrayList<String> fileNames = new ArrayList<>();
		for (Uri uri : uriList) {
			DocumentFile file = DocumentFile.fromSingleUri(getApplicationContext(), uri);
			if (file == null)
				continue;

			String fileName = file.getName();
			fileNames.add(fileName);
		}
		return fileNames;
	}

	private ArrayList<String> getOverwrittenFiles(ArrayList<String> fileNames) {
		ArrayList<String> overwrittenFiles = new ArrayList<>();
		ExternalFilesManager fileManager = new ExternalFilesManager(this);
		for (String fileName : fileNames) {
			if (fileManager.hasFile(fileName))
				overwrittenFiles.add(fileName);
		}
		return overwrittenFiles;
	}

	/**
	 * DETH: imports run on a worker thread (archives can take a while and may ask for a password),
	 * then the activity finishes as before, showing a summary first if something needs explaining.
	 */
	private void startImport(ArrayList<Uri> uriList) {
		importing = true;
		AlertDialog progress = new AlertDialog.Builder(this)
		    .setMessage(R.string.importing_files)
		    .setCancelable(false)
		    .create();
		progress.show();
		new Thread(() -> {
			ArrayList<String> messages = importFiles(uriList);
			runOnUiThread(() -> {
				if (isDestroyed())
					return;
				progress.dismiss();
				if (messages.isEmpty()) {
					finish();
					return;
				}
				AlertDialog.Builder builder = new AlertDialog.Builder(this);
				builder.setMessage(String.join("\n\n", messages));
				builder.setPositiveButton(R.string.ok_button, null);
				builder.setOnDismissListener(dialog -> finish());
				builder.create().show();
			});
		}, "DETH import").start();
	}

	/** Runs on the worker thread. Returns messages for the player (empty when all went well). */
	private ArrayList<String> importFiles(ArrayList<Uri> uriList) {
		ArrayList<String> messages = new ArrayList<>();
		ExternalFilesManager fileManager = new ExternalFilesManager(this);
		ArchiveImporter importer = new ArchiveImporter(new File(fileManager.getExternalFilesDirectory()),
		    getCacheDir(), ImportActivity::isGameFile, this::askPassword);
		importer.setJavaNioAvailable(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O);
		importer.cleanTempDir();
		for (Uri uri : uriList) {
			try {
				if (!importArchive(uri, importer, messages))
					importFile(uri);
			} catch (RuntimeException | LinkageError exception) {
				Log.e("importFiles", "import failed", exception);
				messages.add(getString(R.string.import_failed, String.valueOf(uri.getLastPathSegment()), String.valueOf(exception.getMessage())));
			}
		}
		importer.cleanTempDir();
		return messages;
	}

	/**
	 * DETH: a zip, 7z, tar, gz or xz (e.g. a GOG backup or a folder someone packed) is searched for
	 * game files, which are extracted. Returns false when the file is not an archive.
	 */
	private boolean importArchive(Uri uri, ArchiveImporter importer, ArrayList<String> messages) {
		DocumentFile file = DocumentFile.fromSingleUri(getApplicationContext(), uri);
		String fileName = file == null ? null : file.getName();
		if (fileName == null)
			fileName = String.valueOf(uri.getLastPathSegment());
		if (isGameFile(fileName))
			return false;

		try (InputStream raw = getContentResolver().openInputStream(uri)) {
			if (raw == null)
				return false;
			BufferedInputStream in = new BufferedInputStream(raw, 1 << 16);
			ArchiveImporter.Kind kind = ArchiveImporter.detect(fileName, ArchiveImporter.peek(in, 512));
			switch (kind) {
			case NONE:
				return false;
			case RAR:
				messages.add(getString(R.string.import_rar_unsupported));
				return true;
			case EXE:
				messages.add(getString(R.string.import_exe_unsupported));
				return true;
			default:
				break;
			}
			// commons-compress (7z, tar, gz, xz) needs java.nio.file, which Android has from 8.0 on.
			if (ArchiveImporter.needsJavaNio(kind) && Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
				messages.add(getString(R.string.import_needs_android_8, fileName));
				return true;
			}
			ArchiveImporter.Result result = importer.importStream(in, fileName);
			addMessages(fileName, result, messages);
		} catch (IOException exception) {
			Log.e("importArchive", exception.getMessage() == null ? "IOException" : exception.getMessage(), exception);
			messages.add(getString(R.string.import_failed, fileName, String.valueOf(exception.getMessage())));
		}
		return true;
	}

	private void addMessages(String fileName, ArchiveImporter.Result result, ArrayList<String> messages) {
		for (String error : result.errors) {
			Log.e("importArchive", error);
			messages.add(getString(R.string.import_error, error));
		}
		for (String skipped : result.skipped)
			messages.add(getString(R.string.import_skipped, skipped));
		if (!result.rarArchives.isEmpty())
			messages.add(getString(R.string.import_rar_unsupported));
		if (!result.installers.isEmpty())
			messages.add(getString(R.string.import_exe_unsupported));
		if (result.extracted.isEmpty() && result.errors.isEmpty() && result.skipped.isEmpty() && result.rarArchives.isEmpty())
			messages.add(getString(R.string.import_no_game_files, fileName));
	}

	/**
	 * DETH: called on the worker thread by ArchiveImporter. Shows the password dialog on the UI
	 * thread and waits for it. Returns null when the player cancels.
	 */
	private String askPassword(String archiveName, boolean wrongPassword) {
		final String[] answer = new String[1];
		final CountDownLatch latch = new CountDownLatch(1);
		runOnUiThread(() -> {
			if (isFinishing() || isDestroyed()) {
				latch.countDown();
				return;
			}
			EditText input = new EditText(this);
			input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
			input.setSingleLine(true);
			input.setHint(R.string.password_hint);
			FrameLayout frame = new FrameLayout(this);
			int padding = (int)TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 20, getResources().getDisplayMetrics());
			frame.setPadding(padding, 0, padding, 0);
			frame.addView(input);

			AlertDialog.Builder builder = new AlertDialog.Builder(this);
			builder.setTitle(getString(R.string.password_title, archiveName));
			builder.setMessage(wrongPassword ? R.string.password_wrong : R.string.password_prompt);
			builder.setView(frame);
			builder.setPositiveButton(R.string.ok_button, (dialog, which) -> answer[0] = input.getText().toString());
			builder.setNegativeButton(R.string.cancel_button, null);
			builder.setOnDismissListener(dialog -> latch.countDown());
			AlertDialog dialog = builder.create();
			dialog.setCanceledOnTouchOutside(false);
			dialog.show();
			input.requestFocus();
		});
		try {
			latch.await();
		} catch (InterruptedException exception) {
			Thread.currentThread().interrupt();
			return null;
		}
		return answer[0];
	}

	private static boolean isGameFile(String name) {
		String lower = name.toLowerCase(java.util.Locale.ROOT);
		return lower.equals("diabdat.mpq") || lower.equals("spawn.mpq") || lower.equals("hellfire.mpq")
		    || lower.equals("hfmonk.mpq") || lower.equals("hfmusic.mpq") || lower.equals("hfvoice.mpq")
		    || lower.equals("hfbard.mpq") || lower.equals("hfbarb.mpq") || lower.equals("fonts.mpq")
		    || lower.endsWith(".mpq");
	}

	private void importFile(Uri fileUri) {
		if (fileUri == null)
			return;

		DocumentFile file = DocumentFile.fromSingleUri(getApplicationContext(), fileUri);
		if (file == null)
			return;

		String fileName = file.getName();
		ExternalFilesManager fileManager = new ExternalFilesManager(this);
		File externalFile = fileManager.getFile(fileName);

		try {
			InputStream inputStream = null;
			OutputStream outputStream = null;

			try {
				ContentResolver contentResolver = getContentResolver();
				inputStream = contentResolver.openInputStream(fileUri);
				outputStream = new FileOutputStream(externalFile);

				// Transfer bytes from in to out
				byte[] buf = new byte[4096];
				int len;
				while ((len = inputStream.read(buf)) > 0) {
					outputStream.write(buf, 0, len);
				}
			} finally {
				if (inputStream != null)
					inputStream.close();
				if (outputStream != null)
					outputStream.close();
			}
		} catch (IOException exception) {
			String message = exception.getMessage();
			if (message == null) {
				Log.e("importFile", "IOException", exception);
			} else {
				Log.e("importFile", message);
			}
		}
	}
}
