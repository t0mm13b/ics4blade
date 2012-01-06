
import java.io.*;
import java.util.*;

class CGI {
    public int _cgiptr;
    
    static { 
        JNI.loadLibrary();
    }
    
    public CGI() {
	_cgiptr = _init();
    }

    private static native int _init();
    private static native void parse();
};
