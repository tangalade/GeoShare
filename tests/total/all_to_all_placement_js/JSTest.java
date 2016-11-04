import java.io.File;
import java.io.PrintWriter;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import com.gargoylesoftware.htmlunit.ConfirmHandler;
import com.gargoylesoftware.htmlunit.IncorrectnessListener;
import com.gargoylesoftware.htmlunit.SilentCssErrorHandler;
import com.gargoylesoftware.htmlunit.WebClient;
import com.gargoylesoftware.htmlunit.WebConsole;
import com.gargoylesoftware.htmlunit.html.HtmlAttributeChangeListener;
import com.gargoylesoftware.htmlunit.html.HtmlAttributeChangeEvent;
import com.gargoylesoftware.htmlunit.html.HtmlElement;
import com.gargoylesoftware.htmlunit.html.HtmlForm;
import com.gargoylesoftware.htmlunit.html.HtmlPage;
import com.gargoylesoftware.htmlunit.html.HtmlSelect;
import com.gargoylesoftware.htmlunit.html.HtmlButtonInput;
import com.gargoylesoftware.htmlunit.html.HtmlCheckBoxInput;
import com.gargoylesoftware.htmlunit.html.HtmlFileInput;
import com.gargoylesoftware.htmlunit.html.HtmlPasswordInput;
import com.gargoylesoftware.htmlunit.html.HtmlTextInput;
import com.gargoylesoftware.htmlunit.AlertHandler;
import com.gargoylesoftware.htmlunit.BrowserVersion;
import com.gargoylesoftware.htmlunit.BrowserVersionFeatures;
import com.gargoylesoftware.htmlunit.Page;

public class JSTest
{
    final static int ITERATIONS = 100;

    final String LOG_FILE_PREFIX = "logs/";
    final String LOG_FILE = "lat";
    final String LOG_FILE_SUFFIX = ".csv";

    final String[] URLS = {
	"file:///home/ubuntu/htmlunit/upload_real.html",
	"file:///home/ubuntu/htmlunit/download_real.html",
    };
    final String user_name = "kyle";
    final String password = "aoeu";
    final String bucket_name = "my-bucket";
    final String obj_name_base = "my-object";
    final boolean override = true;

    public boolean done = false;
    
    private class CustomAlertHandler implements AlertHandler
    {
	private String request = "GET", encoding = "NONE", bucket_name = "my-bucket", obj_name = "my-object";
	private int threshold = 0, num_shares = 0, size = 0;
	private PrintWriter writer = null;
	private boolean visible = true;
	public CustomAlertHandler()
	{
	    this("","");
	}
	public CustomAlertHandler(String prefix)
	{
	    this(prefix,"");
	}
	public CustomAlertHandler(String prefix, String suffix)
	{
	    if ( writer != null )
		writer.close();
	    try {
		writer = new PrintWriter(LOG_FILE_PREFIX + prefix + LOG_FILE + suffix + LOG_FILE_SUFFIX);
	    } catch(FileNotFoundException e) {
		System.err.println(e.getMessage());
	    }
	}
	public void setVisible(boolean visible)
	{
	    this.visible = visible;
	}
	public void setParams(String request,
			      String encoding,
			      int threshold,
			      int num_shares,
			      int size,
			      String bucket_name,
			      String obj_name)
	{
	    this.request = request;
	    this.encoding = encoding;
	    this.threshold = threshold;
	    this.num_shares = num_shares;
	    this.size = size;
	    this.bucket_name = bucket_name;
	    this.obj_name = obj_name;
	}
	public void setObjectName(String obj_name)
	{
	    this.obj_name = obj_name;
	}
	public void setEncoding(String encoding, int threshold, int num_shares)
	{
	    this.encoding = encoding;
	    this.threshold = threshold;
	    this.num_shares = num_shares;
	}
	public void setRequest(String request)
	{
	    this.request = request;
	}
	public void setSize(int size)
	{
	    this.size = size;
	}
			      
	public void handleAlert(Page page, String message)
	{
	    String out = request + "," +
		encoding + "," +
		threshold + "," +
		num_shares + "," +
		size + "," +
		bucket_name + "," +
		obj_name + "," +
		message;
	    System.out.println(out);
	    if ( writer != null )
	    {
		writer.println(out);
		// must flush, or won't appear for a while
		writer.flush();
	    }
	}
    };
    private class SimpleConfirmHandler implements ConfirmHandler
    {
	public boolean handleConfirm(Page page, String message)
	{
	    System.out.println("Confirming: " + message);
	    return true;
	}
    };
    private class CustomConsoleLogger implements WebConsole.Logger
    {
	private int level = 0;
	public CustomConsoleLogger(int level)
	{
	    this.level = level;
	};
	public void error(Object message) {
	    if ( level > 0 )
		System.out.println("ERROR: " + message);
	};
	public void warn(Object message) {
	    if ( level > 1 )
		System.out.println("WARN: " + message);
	};
	public void info(Object message) {
	    if ( level > 2 )
		System.out.println("Info: " + message);
	};
	public void debug(Object message) {
	    if ( level > 3 )
		System.out.println("Debug: " + message);
	};
	public void trace(Object message) {
	    if ( level > 4 )
		System.out.println("Trace: " + message);
	};
    };
    private class StatusListener implements HtmlAttributeChangeListener
    {
	public StatusListener()
	{
	};
	public void attributeRemoved(HtmlAttributeChangeEvent event)
	{
	    System.out.println("REMOVED " + event.getName() + ": " + event.getValue());
	}
	public void attributeAdded(HtmlAttributeChangeEvent event)
	{
//	    System.out.println("ADDED " + event.getName() + ": " + event.getValue());
	    done = true;
	}
	public void attributeReplaced(HtmlAttributeChangeEvent event)
	{
//	    System.out.println("REPLACED " + event.getName() + ": " + event.getValue());
	    done = true;
	}
    };
    private class Encoding
    {
	public String scheme = "NONE";
	public int threshold = 1;
	public int num_shares = 1;
	public Encoding(){};
	public Encoding(String scheme, int threshold, int num_shares)
	{
	    this.scheme = scheme;
	    this.threshold = threshold;
	    this.num_shares = num_shares;
	};
	public Encoding(String scheme)
	{
	    this.scheme = scheme;
	};
	public String toString()
	{
	    return scheme + "(" + threshold + "," + num_shares + ")";
	};
    };
    private final String genObjName(int iter,Encoding scheme)
    {
	return "my-object-" + scheme.scheme + scheme.threshold + scheme.num_shares + "-" + iter;
    }
    public void runTests(int[] obj_sizes, 
			   Encoding[] encodings,
			   int iterations)
	throws IOException
    {
	CustomAlertHandler alertHandler = new CustomAlertHandler();
	java.util.logging.Logger.getLogger("com.gargoylesoftware.htmlunit.javascript.StrictErrorReporter").setLevel(java.util.logging.Level.OFF);
	// if doing concurrent testing, need a separate WebClient for each thread
	//   each WebClient only has one active page, which is set when getPage is called
	//   need a separate CustomAlertHandler for each WebClient then as well
        final WebClient webClient = new WebClient(BrowserVersion.INTERNET_EXPLORER_11);
	webClient.setCssErrorHandler(new SilentCssErrorHandler());
	webClient.setIncorrectnessListener(new IncorrectnessListener()
	    { public void notify(String arg0, Object arg1) {}; });
        webClient.setAlertHandler(alertHandler);
	webClient.setConfirmHandler(new SimpleConfirmHandler());
	webClient.getWebConsole().setLogger(new CustomConsoleLogger(1));
	
	alertHandler.setParams("SET","NONE",1,1,1,"my-bucket","my-object");
	for ( int obj_size: obj_sizes )
	{
	    alertHandler.setSize(obj_size);
	    HtmlPage page;
	    System.out.print(obj_size + ": ");
	    for ( Encoding encoding: encodings )
	    {
		alertHandler.setEncoding(encoding.scheme, encoding.threshold, encoding.num_shares);
		System.out.print(encoding.toString() + ": ");
		alertHandler.setRequest("SET");
		try {
		    page = webClient.getPage(URLS[0]);
		} catch(IOException e) {
		    System.err.println("Exception in getPage()");
		    throw e;
		}
		for ( int iter=0; iter<iterations; iter++ )
		{
		    System.out.println(iter);
		    alertHandler.setObjectName(genObjName(iter,encoding));
		    uploadTest(page, obj_size, encoding, genObjName(iter,encoding));
		}
//		System.out.println(page.getHtmlElementById("js_status_window").getTextContent());
		alertHandler.setRequest("GET");
		try {
		    page = webClient.getPage(URLS[1]);
		} catch(IOException e) {
		    System.err.println("Exception in getPage()");
		    throw e;
		}
		for ( int iter=0; iter<iterations; iter++ )
		{
		    alertHandler.setObjectName(genObjName(iter,encoding));
		    downloadTest(page, genObjName(iter,encoding));
		}
//		System.out.println(page.getHtmlElementById("js_status_window").getTextContent());
	    }
	}
        webClient.closeAllWindows();
    }

    public void uploadTest(HtmlPage page, int obj_size, 
			   Encoding encoding, String obj_name)
	throws IOException
    {
	page.getHtmlElementById("submit_status").addHtmlAttributeChangeListener(new StatusListener());

	page.<HtmlTextInput>getHtmlElementById("user_name_in").setValueAttribute(user_name);
	page.<HtmlPasswordInput>getHtmlElementById("password_in").setValueAttribute(password);
	page.<HtmlTextInput>getHtmlElementById("bucket_name_in").setValueAttribute(bucket_name);
	page.<HtmlTextInput>getHtmlElementById("obj_name_in").setValueAttribute(obj_name);
	// file input not working, javascript will just create random data of the correct size
	page.<HtmlFileInput>getHtmlElementById("file_in").setValueAttribute(Integer.toString(obj_size));
	// javascript will just use hard-coded credentials, since file input does not work
	//page.<HtmlFileInput>getHtmlElementById("cred_in").setValueAttribute();
	page.<HtmlCheckBoxInput>getHtmlElementById("override_in").setChecked(override);
	page.<HtmlSelect>getHtmlElementById("encoding_in").setSelectedAttribute(encoding.scheme,true);
	page.<HtmlTextInput>getHtmlElementById("threshold_in")
	    .setValueAttribute(Integer.toString(encoding.threshold));
	page.<HtmlTextInput>getHtmlElementById("num_shares_in")
	    .setValueAttribute(Integer.toString(encoding.num_shares));
	
	long start = System.currentTimeMillis();
	try {
	    page.getHtmlElementById("submit_button").click();
	} catch(IOException e) {
	    System.err.println("Exception in clicking submit_button");
	    throw e;
	}
	while ( !done )
	{
	    try {
		Thread.sleep(10);
	    } catch(InterruptedException e) {
		System.err.println("Error in Thread.sleep");
	    }
	}
	long end = System.currentTimeMillis();
	System.out.print(Long.toString(end-start) + "ms, ");
	done = false;
    };

    public void downloadTest(HtmlPage page, String obj_name)
	throws IOException
    {
	page.getHtmlElementById("submit_status").addHtmlAttributeChangeListener(new StatusListener());

	page.<HtmlTextInput>getHtmlElementById("user_name_in").setValueAttribute(user_name);
	page.<HtmlPasswordInput>getHtmlElementById("password_in").setValueAttribute(password);
	page.<HtmlTextInput>getHtmlElementById("bucket_name_in").setValueAttribute(bucket_name);
	page.<HtmlTextInput>getHtmlElementById("obj_name_in").setValueAttribute(obj_name);
	// javascript will just use hard-coded credentials, since file input does not work
	//page.<HtmlFileInput>getHtmlElementById("cred_in").setValueAttribute();
	
	long start = System.currentTimeMillis();
	try {
	    page.getHtmlElementById("submit_button").click();
	} catch(IOException e) {
	    System.err.println("Exception in clicking submit_button");
	    throw e;
	}
	while ( !done )
	{
	    try {
		Thread.sleep(10);
	    } catch(InterruptedException e) {
		System.err.println("Error in Thread.sleep");
	    }
	}
	long end = System.currentTimeMillis();
	System.out.print(Long.toString(end-start) + "ms, ");
	done = false;
    };

    public static void main(String[] args)
    {
        JSTest test = new JSTest();

	int[] obj_sizes = {
	    (int)Math.pow(2,15),
	};
	Encoding[] encodings = {
	    //	    test.new Encoding(),
	    test.new Encoding("SSS",3,5),
	};
	
        try {
            test.runTests(obj_sizes, encodings, ITERATIONS);
        } catch (Exception e) {
            System.err.println("Exception: " + e.getMessage());
        }
    }
}
