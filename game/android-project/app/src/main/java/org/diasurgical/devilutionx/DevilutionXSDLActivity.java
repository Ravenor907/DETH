package org.diasurgical.devilutionx;

import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;
import android.content.pm.PackageInstaller;
import android.graphics.Insets;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.ViewTreeObserver;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;

import org.libsdl.app.SDLActivity;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Locale;

public class DevilutionXSDLActivity extends SDLActivity {
	private ExternalFilesManager fileManager;
	private boolean noExit;

	protected void onCreate(Bundle savedInstanceState) {
		// windowSoftInputMode=adjustPan stopped working
		// for fullscreen apps after Android 7.0
		if (Build.VERSION.SDK_INT >= 25)
			trackVisibleSpace();

		// Force app to overlap with the display cutout
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
			getWindow().getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;

		fileManager = new ExternalFilesManager(this);

		migrateSaveGames();

		super.onCreate(savedInstanceState);

		askForSharedSaves();
	}

	// --- DETH: characters in Downloads/DETH/saves (kept when the app is uninstalled) ---------

	private static final int REQUEST_STORAGE = 4901;

	private static boolean hasSharedStorageAccess(Context context) {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
			return Environment.isExternalStorageManager();
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
			return context.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
		return true;
	}

	/**
	 * Called from C++: the shared saves folder when DETH may write there, otherwise "" (the game
	 * then keeps saves in its own storage and copies them over once access is given).
	 */
	public static String dethSharedSavesDir() {
		Context context = SDLActivity.getContext();
		if (context == null || !hasSharedStorageAccess(context))
			return "";
		File dir = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "DETH/saves");
		if (!dir.isDirectory() && !dir.mkdirs())
			return "";
		return dir.getAbsolutePath();
	}

	/** Asks once to keep characters in Downloads/DETH/saves. */
	private void askForSharedSaves() {
		if (hasSharedStorageAccess(this))
			return;
		SharedPreferences prefs = getSharedPreferences("deth", MODE_PRIVATE);
		if (prefs.getBoolean("askedSharedSaves", false))
			return;
		prefs.edit().putBoolean("askedSharedSaves", true).apply();
		new AlertDialog.Builder(this)
		    .setTitle("Keep your characters safe")
		    .setMessage("DETH can save your characters in Downloads/DETH/saves. They then stay when the app is "
		        + "uninstalled and are easy to copy to another device.\n\nAndroid asks you to allow file access "
		        + "for DETH next. You can turn it on later in the app's settings.")
		    .setPositiveButton("Allow", (dialog, which) -> requestSharedStorageAccess())
		    .setNegativeButton("Not now", null)
		    .show();
	}

	private void requestSharedStorageAccess() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
			try {
				Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName()));
				startActivity(intent);
			} catch (Exception e) {
				startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
			}
		} else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
			requestPermissions(new String[] { android.Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQUEST_STORAGE);
		}
	}

	/**
	 * On app launch make sure the game data is present
	 */
	protected void onStart() {
		super.onStart();

		if (isMissingGameData() || DataActivity.needsFirstRunScreen(this, fileManager)) {
			Intent intent = new Intent(this, DataActivity.class);
			startActivity(intent);
			noExit = true;
			this.finish();
		}
	}

	/**
	 * DETH: called from the main menu ("Import Game Files"). Leaves the game for the
	 * importer screen; that screen starts the game again afterwards.
	 */
	public void importGameFiles() {
		runOnUiThread(() -> {
			Intent intent = new Intent(this, DataActivity.class);
			intent.putExtra(DataActivity.EXTRA_IMPORT, true);
			startActivity(intent);
			noExit = true;
			finish();
		});
	}

	/**
	 * When the user exits the game, use System.exit(0)
	 * to clear memory and prevent errors on restart
	 */
	protected void onDestroy() {
		super.onDestroy();

		if (!noExit) {
			System.exit(0);
		}
	}

	private void trackVisibleSpace() {
		this.getWindow().getDecorView().getViewTreeObserver().addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener() {
			@Override
			public void onGlobalLayout() {
				SurfaceView surface = mSurface;
				SurfaceHolder holder = surface.getHolder();

				if (!isIMEVisible()) {
					holder.setSizeFromLayout();
					return;
				}

				// Software keyboard may encroach on the app's visible space so
				// force the drawing surface to fit in the visible display frame
				Rect visibleSpace = new Rect();
				getVisibleSpaceAroundIME(visibleSpace);
				holder.setFixedSize(visibleSpace.width(), visibleSpace.height());
			}

			private boolean isIMEVisible() {
				// Window insets are not available before Android 11 so
				// we must always assume the software keyboard is visible
				if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
					return true;

				WindowInsets rootInsets = getWindow().getDecorView().getRootWindowInsets();
				return rootInsets.isVisible(WindowInsets.Type.ime());
			}

			private void getVisibleSpaceAroundIME(Rect visibleSpace) {
				if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
					getWindow().getDecorView().getWindowVisibleDisplayFrame(visibleSpace);
					return;
				}

				WindowInsets rootInsets = getWindow().getDecorView().getRootWindowInsets();
				Insets imeInsets = rootInsets.getInsets(WindowInsets.Type.ime());
				DisplayMetrics realMetrics = new DisplayMetrics();
				getDisplay().getRealMetrics(realMetrics);

				visibleSpace.top = imeInsets.top;
				visibleSpace.left = imeInsets.left;
				visibleSpace.right = realMetrics.widthPixels - imeInsets.right;
				visibleSpace.bottom = realMetrics.heightPixels - imeInsets.bottom;
			}
		});
	}

	private boolean isMissingGameData() {
		String lang = Locale.getDefault().toString();
		if (lang.startsWith("pl") && !fileManager.hasFile("pl.mpq"))
			return true;
		if (lang.startsWith("ru") && !fileManager.hasFile("ru.mpq"))
			return true;
		File fonts_mpq = fileManager.getFile("/fonts.mpq");
		if (lang.startsWith("ko") || lang.startsWith("zh") || lang.startsWith("ja") || fonts_mpq.exists()) {
			if (!fonts_mpq.exists() || areFontsOutOfDate(fonts_mpq.getAbsolutePath()))
				return true;
		}

		if (fileManager.hasFile("diabdat.mpq") || fileManager.hasFile("DIABDAT.MPQ"))
			return false;
		return !fileManager.ensureBundledShareware();
	}

	private void migrateSaveGames() {
		File[] files = getFilesDir().listFiles();
		if (files == null)
			return;
		for (File internalFile : files) {
			fileManager.migrateFile(internalFile);
		}
	}

	/**
	 * This method is called by SDL using JNI.
	 */
	public String getLocale()
	{
		return Locale.getDefault().toString();
	}

	protected String[] getArguments() {
		String externalDir = fileManager.getExternalFilesDirectory();

		if (BuildConfig.DEBUG) {
			return new String[]{
				"--data-dir",
				externalDir,
				"--config-dir",
				externalDir,
				"--save-dir",
				externalDir,
				"--verbose",
			};
		}

		return new String[]{
			"--data-dir",
			externalDir,
			"--config-dir",
			externalDir,
			"--save-dir",
			externalDir
		};
	}

	protected String[] getLibraries() {
		return new String[]{
				"zt", // online play (ZeroTier); loaded first so older Android versions find it
				"devilutionx"
		};
	}

	public static native boolean areFontsOutOfDate(String fonts_mpq);

	// --- DETH in-game updater (called from C++ via JNI) ---------------------------------

	private static HttpURLConnection dethOpen(String url) throws java.io.IOException {
		HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
		connection.setRequestProperty("User-Agent", "DETH");
		connection.setRequestProperty("Accept", "application/vnd.github+json");
		connection.setConnectTimeout(15000);
		connection.setReadTimeout(30000);
		connection.setInstanceFollowRedirects(true);
		return connection;
	}

	/** Returns the response body, or null on any error. */
	public static String dethHttpGet(String url) {
		try {
			HttpURLConnection connection = dethOpen(url);
			if (connection.getResponseCode() != 200)
				return null;
			try (InputStream in = connection.getInputStream()) {
				ByteArrayOutputStream out = new ByteArrayOutputStream();
				byte[] buffer = new byte[16384];
				int n;
				while ((n = in.read(buffer)) > 0)
					out.write(buffer, 0, n);
				return out.toString("UTF-8");
			}
		} catch (Exception e) {
			return null;
		}
	}

	/** Downloads url to path, following GitHub's redirect to its file host. */
	public static boolean dethDownload(String url, String path) {
		try {
			HttpURLConnection connection = dethOpen(url);
			connection.setReadTimeout(120000);
			int redirects = 0;
			while ((connection.getResponseCode() / 100) == 3 && redirects++ < 5) {
				String location = connection.getHeaderField("Location");
				connection.disconnect();
				connection = dethOpen(location);
				connection.setReadTimeout(120000);
			}
			if (connection.getResponseCode() != 200)
				return false;
			try (InputStream in = connection.getInputStream(); OutputStream out = new FileOutputStream(path)) {
				byte[] buffer = new byte[65536];
				int n;
				while ((n = in.read(buffer)) > 0)
					out.write(buffer, 0, n);
			}
			return true;
		} catch (Exception e) {
			return false;
		}
	}

	private static final String DETH_INSTALL_ACTION = "org.diasurgical.devilutionx.UPDATE_STATUS";
	private static BroadcastReceiver dethInstallReceiver;

	/** Hands the APK to Android's package installer; Android asks the player to confirm. */
	public static boolean dethInstallApk(String path, String version) {
		final SDLActivity activity = mSingleton;
		if (activity == null)
			return false;
		try {
			if (dethInstallReceiver == null) {
				dethInstallReceiver = new BroadcastReceiver() {
					@Override
					public void onReceive(Context context, Intent intent) {
						int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
						if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
							Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
							if (confirm != null) {
								confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
								context.startActivity(confirm);
							}
						}
					}
				};
				IntentFilter filter = new IntentFilter(DETH_INSTALL_ACTION);
				if (Build.VERSION.SDK_INT >= 33)
					activity.registerReceiver(dethInstallReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
				else
					activity.registerReceiver(dethInstallReceiver, filter);
			}
			PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
			PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
			params.setAppPackageName(activity.getPackageName());
			int sessionId = installer.createSession(params);
			try (PackageInstaller.Session session = installer.openSession(sessionId)) {
				File apk = new File(path);
				try (InputStream in = new FileInputStream(apk); OutputStream out = session.openWrite("deth.apk", 0, apk.length())) {
					byte[] buffer = new byte[65536];
					int n;
					while ((n = in.read(buffer)) > 0)
						out.write(buffer, 0, n);
					session.fsync(out);
				}
				Intent intent = new Intent(DETH_INSTALL_ACTION).setPackage(activity.getPackageName());
				int flags = PendingIntent.FLAG_UPDATE_CURRENT;
				if (Build.VERSION.SDK_INT >= 31)
					flags |= PendingIntent.FLAG_MUTABLE;
				PendingIntent pending = PendingIntent.getBroadcast(activity, sessionId, intent, flags);
				session.commit(pending.getIntentSender());
			}
			return true;
		} catch (Exception e) {
			return false;
		}
	}

	/** DETH: delivers the text entry dialog's result to the game (ok=false when cancelled). */
	public static native void textInputDone(String text, boolean ok);

	/** DETH: edit field kinds, matching UiEdit::InputKind on the native side. */
	private static final int INPUT_TEXT = 0;
	private static final int INPUT_ADDRESS = 1;
	private static final int INPUT_PASSWORD = 2;

	/**
	 * DETH: menu text fields (hero name, address, password) use a native dialog: full-size
	 * readable text, a keyboard that stays up, and OK/Cancel. Called from the game thread.
	 */
	public void showTextInput(String title, String value, int maxLength, int kind) {
		runOnUiThread(() -> {
			final EditText edit = new EditText(this);
			edit.setSingleLine(true);
			edit.setText(value);
			edit.setSelection(edit.getText().length());
			edit.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
			edit.setFilters(new InputFilter[] { new InputFilter.LengthFilter(Math.max(1, maxLength)) });
			int type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
			if (kind == INPUT_ADDRESS)
				type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
			else if (kind == INPUT_PASSWORD)
				type = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
			edit.setInputType(type);
			edit.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);

			final FrameLayout frame = new FrameLayout(this);
			final int pad = (int)(20 * getResources().getDisplayMetrics().density);
			frame.setPadding(pad, pad / 2, pad, 0);
			frame.addView(edit);

			final boolean[] answered = { false };
			final AlertDialog dialog = new AlertDialog.Builder(this)
				.setTitle(title)
				.setView(frame)
				.setPositiveButton(android.R.string.ok, (d, w) -> {
					answered[0] = true;
					textInputDone(edit.getText().toString(), true);
				})
				.setNegativeButton(android.R.string.cancel, null)
				.create();
			dialog.setOnDismissListener(d -> {
				if (!answered[0])
					textInputDone("", false);
			});
			edit.setOnEditorActionListener((v, actionId, event) -> {
				if (actionId == EditorInfo.IME_ACTION_DONE
				    || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER && event.getAction() == KeyEvent.ACTION_DOWN)) {
					answered[0] = true;
					textInputDone(edit.getText().toString(), true);
					dialog.dismiss();
					return true;
				}
				return false;
			});
			dialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
			dialog.show();
			edit.requestFocus();
		});
	}

	/** DETH: a short tick when a menu item is tapped. Follows the system touch-feedback setting. */
	public static void tapFeedback() {
		final SDLActivity activity = mSingleton;
		if (activity == null || mSurface == null)
			return;
		activity.runOnUiThread(() -> {
			if (mSurface != null)
				mSurface.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
		});
	}
}
