package com.cyanogenmod.BladeParts;

import com.cyanogenmod.BladeParts.R;

import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceActivity;
import android.preference.PreferenceManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

public class BladeParts extends PreferenceActivity {

	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		addPreferencesFromResource(R.xml.bladeparts);
	}

   private void writeValue(String parameter, int value) {
      try {
          FileOutputStream fos = new FileOutputStream(new File(parameter));
          fos.write(String.valueOf(value).getBytes());
          fos.flush();
          fos.getFD().sync();
          fos.close();
      } catch (FileNotFoundException e) {
         e.printStackTrace();
      } catch (IOException e) {
         e.printStackTrace();
      }
   }

   @Override
   public void onPause() {
      super.onPause();
      SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getBaseContext());
      // USB charging
      if(prefs.getBoolean("usb_charging", true))
         writeValue("/sys/module/msm_battery/parameters/usb_chg_enable", 1);
      else
         writeValue("/sys/module/msm_battery/parameters/usb_chg_enable", 0);
   }
}
