package com.cyanogenmod.BladeParts;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

public class BladePartsStartup extends BroadcastReceiver
{
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
   public void onReceive(final Context context, final Intent bootintent) {
      SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
      // USB charging
      if(prefs.getBoolean("usb_charging", true))
         writeValue("/sys/module/msm_battery/parameters/usb_chg_enable", 1);
      else
         writeValue("/sys/module/msm_battery/parameters/usb_chg_enable", 0);
   }
}
