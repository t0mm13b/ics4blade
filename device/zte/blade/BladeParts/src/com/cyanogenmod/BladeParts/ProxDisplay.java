package com.cyanogenmod.BladeParts;

import java.io.BufferedInputStream;
import java.io.BufferedReader;
import java.io.InputStreamReader;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.TextView;

public class ProxDisplay extends Activity implements OnClickListener {

	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		setContentView(R.layout.prox_display);

		View backButton = findViewById(R.id.back_button);
		backButton.setOnClickListener(this);

		TextView textBox = new TextView(this);
		textBox = (TextView) findViewById(R.id.textBox);

		try {
			Process p = Runtime.getRuntime().exec("/system/bin/prox_cal -d");
			BufferedReader commandResult = new BufferedReader(
					new InputStreamReader(new BufferedInputStream(
							p.getInputStream())));
			p.waitFor();
			String returned;
			while ((returned = commandResult.readLine()) != null) {
				textBox.append(returned + "\n");
			}

		} catch (Exception ex) {
			textBox.setText("Error: " + ex.getMessage());
		}
	}

	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.back_button:
			finish();
		}
	}
}
