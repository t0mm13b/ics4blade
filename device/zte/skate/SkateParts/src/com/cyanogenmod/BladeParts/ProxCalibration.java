package com.cyanogenmod.BladeParts;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.InputStreamReader;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

public class ProxCalibration extends Activity {
	final private static String TAG = "ProxCal";

	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		Toast toast;
		try {
			Process p = Runtime.getRuntime().exec("/system/bin/prox_cal -c");
			BufferedReader commandResult = new BufferedReader(
					new InputStreamReader(new BufferedInputStream(
							p.getInputStream())));
			p.waitFor();
			String returned = commandResult.readLine();
			Log.d(TAG, returned);
			toast = Toast.makeText(ProxCalibration.this, returned,
					Toast.LENGTH_LONG);

		} catch (Exception ex) {
			toast = Toast.makeText(ProxCalibration.this,
					getString(R.string.toast_error) + " " + ex.getMessage(),
					Toast.LENGTH_LONG);
		}

		toast.show();
		finish();

	}

}
