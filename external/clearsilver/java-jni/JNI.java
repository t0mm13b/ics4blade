package org.clearsilver;

/**
 * Loads the ClearSilver JNI library.
 *
 * <p>By default, it attempts to load the library 'clearsilver-jni' from the
 * path specified in the 'java.library.path' system property.</p>
 *
 * <p>If this fails, the JVM exits with a code of 1. However, this strategy
 * can be changed using {@link #setFailureCallback(Runnable)}.</p>
 */
public class JNI {
    
  /**
   * Failure callback strategy that writes a message to sysout, then calls
   * System.exit(1). 
   */
  public static Runnable EXIT_JVM = new Runnable() {
    public void run() {
      System.out.println("Could not load '" + libraryName + "'");
      System.out.println("java.library.path = " 
          + System.getProperty("java.library.path"));
      System.exit(1);
    }
  };

  /**
   * Failure callback strategy that throws an UnsatisfiedLinkError, which 
   * should be caught be client code.
   */
  public static Runnable THROW_ERROR = new Runnable() {
    public void run() {
      throw new UnsatisfiedLinkError("Could not load '" + libraryName + "'");
    }
  };

  private static Runnable failureCallback = EXIT_JVM;

  private static Object callbackLock = new Object();
  
  private static String libraryName = "clearsilver-jni";
        
  /**
   * Attempts to load the ClearSilver JNI library.
   *
   * @see #setFailureCallback(Runnable)
   */
  public static void loadLibrary() {
    try {
      System.loadLibrary(libraryName);
    } catch (UnsatisfiedLinkError e) {
      synchronized (callbackLock) {
        if (failureCallback != null) {
          failureCallback.run();
        }
      }
    }    
  }
  
  /**
   * Sets a callback for what should happen if the JNI library cannot
   * be loaded. The default is {@link #EXIT_JVM}.
   *
   * @see #EXIT_JVM
   * @see #THROW_ERROR
   */
  public static void setFailureCallback(Runnable failureCallback) {
    synchronized(callbackLock) {
      JNI.failureCallback = failureCallback;
    }
  }

  /**
   * Set name of JNI library to load. Default is 'clearsilver-jni'.
   */
  public static void setLibraryName(String libraryName) {
    JNI.libraryName = libraryName;
  }

}