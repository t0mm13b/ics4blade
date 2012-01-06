package org.clearsilver;

import java.util.LinkedList;
import java.util.List;
import java.io.File;

/**
 * Utility class containing helper methods
 *
 * @author smarti@google.com (Sergio Marti)
 */
public class CSUtil {

  private CSUtil() { }

  public static final String HDF_LOADPATHS = "hdf.loadpaths";

  /**
   * Helper function that returns a concatenation of the loadpaths in the
   * provided HDF.
   * @param hdf an HDF structure containing load paths.
   * @return A list of loadpaths in order in which to search.
   * @throws NullPointerException if no loadpaths are found.
   */
  public static List<String> getLoadPaths(HDF hdf) {
    List<String> list = new LinkedList<String>();
    HDF loadpathsHdf = hdf.getObj(HDF_LOADPATHS);
    if (loadpathsHdf == null) {
      throw new NullPointerException("No HDF loadpaths located in the specified"
          + " HDF structure");
    }
    for (HDF lpHdf = loadpathsHdf.objChild(); lpHdf != null;
        lpHdf = lpHdf.objNext()) {
      list.add(lpHdf.objValue());
    }
    return list;
  }

  /**
   * Given an ordered list of directories to look in, locate the specified file.
   * Returns <code>null</code> if file not found.
   * @param loadpaths the ordered list of paths to search.
   * @param filename the name of the file.
   * @return a File object corresponding to the file. <code>null</code> if
   * file not found.
   */
  public static File locateFile(List<String> loadpaths, String filename) {
    if (filename == null) {
      throw new NullPointerException("No filename provided");
    }
    if (loadpaths == null) {
      throw new NullPointerException("No loadpaths provided.");
    }
    for (String path : loadpaths) {
      File file = new File(path, filename);
      if (file.exists()) {
        return file;
      }
    }
    return null;
  }
}
