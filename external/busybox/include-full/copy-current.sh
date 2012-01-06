# after .config setup, type "make prepare" to generate /include files
# this step is no more required to build busybox, it is made automatically
# in Android.mk (busybox_prepare module)

cp ../.config ../.config-full

cp ../include/applets.h ./
cp ../include/applet_tables.h ./
cp ../include/autoconf.h ./
cp ../include/bbconfigopts_bz2.h ./
cp ../include/bbconfigopts.h ./
cp ../include/NUM_APPLETS.h ./
cp ../include/usage_compressed.h ./
