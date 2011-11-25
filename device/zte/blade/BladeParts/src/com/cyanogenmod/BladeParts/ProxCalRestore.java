package com.cyanogenmod.BladeParts;

import java.io.File;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

public class ProxCalRestore extends Activity {
	final private static String TAG = "ProxCal";
	final private static String fileName = "/data/misc/prox_data.txt";


	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		Toast toast;
		File f = new File(fileName);

		if(f.exists())
		    if(f.delete())
			toast = Toast.makeText(ProxCalRestore.this, getString(R.string.delete_success), Toast.LENGTH_LONG);
		    else toast = Toast.makeText(ProxCalRestore.this, getString(R.string.delete_error), Toast.LENGTH_LONG);
		else toast = Toast.makeText(ProxCalRestore.this, getString(R.string.delete_no_file), Toast.LENGTH_LONG);

		toast.show();
		finish();

	}

}
