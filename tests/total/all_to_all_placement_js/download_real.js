var user_name_g, password_hash_g;
var bucket_name_g, obj_name_g;
var placements_g;
var data_g;

var encoding_g;
var num_shares_g = 5, threshold_g = 3;

var start_global_g;

// maximum concurrent requests
var MAX_ACTIVE_REQUESTS = 8;
// how many requests are currently being processed
var active_requests_g = 0;
// how many completed requests had errors
var error_requests_g = 0;
// how many shares do not have any running requests
var remaining_shares_g = 0;
var download_complete = 0;

// user, obj_name, scheme, num_shares, thresh, excluded=use1,euw1, src_reg
// only works with IE7+
requestMetadata = function(user_name, password_hash,
			   bucket_name, obj_name, src_reg)
{
    var placements = [];
    var scheme, threshold, num_shares, error;
    xmlhttp = new XMLHttpRequest();

    var url = "http://54.173.39.128:8000/get" +
	"?user="       + user_name +
	"&pass="       + password_hash +
	"&bucketname="    + bucket_name +
	"&objname="    + obj_name +
	"&src_reg="    + src_reg;
    //Status.concat(url + "<br>");

    // set to false if synchronous, but then you are stuck if server isn't running or something goes wrong with request
    xmlhttp.open("GET",url,false);
    xmlhttp.send();

    //Status.concat("Received response: " + xmlhttp.responseText + "<br>");
    var jsonObj = JSON.parse(xmlhttp.responseText);
    scheme = jsonObj.scheme;
    threshold = jsonObj.threshold;
    num_shares = jsonObj.num_shares;
    if ( jsonObj.buckets )
	for ( i=0; i<jsonObj.buckets.length; i++ )
    	    placements[i] = {bucket: jsonObj.buckets[i],
    			     name: jsonObj.objects[i]};
    error = jsonObj.error;
    
//    for ( i=0; i<placements.length; i++ )
//        Status.concat("placements[" + i + "]: " + placements[i] + "<br>");

    return [placements, scheme, threshold, num_shares, error];
};

downloadNextShare = function()
{
    if ( remaining_shares_g == 0 )
	return;
    var idx = num_shares_g - remaining_shares_g;
    var bucket = new AWS.S3({
	params: {Bucket: placements_g[idx].bucket},
    });
    var params = {
	Key: placements_g[idx].name,
    };
    var request = bucket.getObject(params);
    active_requests_g++;
    remaining_shares_g--;
    // HTMLUNIT
//    alert("  " + (idx+1) + ": " + placements_g[idx].bucket + "/" + placements_g[idx].name);
//    Status.concat("&nbsp;&nbsp;".concat(idx+1,": ",placements_g[idx].bucket,"/",placements_g[idx].name,"<br>"));
    request.
	on('success', function(resp) {
	    if ( download_complete )
		return;
	    active_requests_g--;
	    data_g[data_g.length] = resp.data.Body.toString();
	    if ( num_shares_g - remaining_shares_g - active_requests_g - error_requests_g >= threshold_g )
	    {
		// HTMLUNIT
//		alert("Finished Downloading, " + error_requests_g + " failed requests");
		alert("Downloading," + (Date.now()-start_g));
//		Status.concat("Finished downloading, " + error_requests_g + " failed requests<br>");
		download_complete = 1;
		var plain = decodeData(data_g);
		// HTMLUNIT
		document.getElementById("submit_status").setAttribute("title","1");
		// HTMLUNIT
		alert("Total," + (Date.now()-start_global_g));
		var bytes = new Uint8Array(plain.length);
		for ( i=0; i<plain.length; i++ )
		    bytes[i] = plain.charCodeAt(i);
		var plain_blob = new Blob([bytes]);
		var download_link = document.getElementById("decoded_data_download_link");
		download_link.innerHTML = "Temporary download link";
		download_link.href = URL.createObjectURL(plain_blob);
		download_link.download = obj_name_g;
		error_requests = 0;
		return;
	    }
	    // even with active requests and shares still remaining to be downloaded, not possible to get enough
	    if ( num_shares_g - error_requests_g < threshold_g )
	    {
		Status.concat("Failed to download enough fragments for data recovery<br>");
		document.getElementById("submit_status").innerHTML = "Download failed";
		error_requests = 0;
		return;
	    }

	    if ( remaining_shares_g > 0 )
		downloadNextShare();
	}).
	on('error', function(err,resp) {
	    if ( download_complete )
		return;
	    active_requests_g--;
	    error_requests_g++;
	    Status.concat("Error downloading...".concat(err,"<br>"));
	    // (should never happen, but just in case) enough have been downloaded to decode data
	    if ( num_shares_g - remaining_shares_g - active_requests_g - error_requests_g >= threshold_g )
	    {
		// HTMLUNIT
//		alert("Finished Downloading, " + error_requests_g + " failed requests");
		alert("Downloading," + (Date.now()-start_g));
//		Status.concat("Finished downloading, " + error_requests_g + " failed requests<br>");
		download_complete = 1;
		var plain = decodeData(data_g);
		// HTMLUNIT
		document.getElementById("submit_status").setAttribute("title","1");
		// HTMLUNIT
		alert("Total," + (Date.now()-start_global_g));
		var bytes = new Uint8Array(plain.length);
		for ( i=0; i<plain.length; i++ )
		    bytes[i] = plain.charCodeAt(i);
		var plain_blob = new Blob([plain]);
		var download_link = document.getElementById("decoded_data_download_link");
		download_link.innerHTML = "Temporary download link";
		download_link.href = URL.createObjectURL(plain_blob);
		download_link.download = obj_name_g + ".txt";
		error_requests = 0;
		return;
	    }
	    // even with shares still remaining to be downloaded, not possible to get enough
	    if ( num_shares_g - active_requests_g - error_requests_g < threshold_g )
	    {
		// HTMLUNIT
//		alert("Failed to download enough fragments for data recovery");
		alert("Downloading," + (Date.now()-start_g));
		document.getElementById("submit_status").setAttribute("title","1");
		// HTMLUNIT
		alert("Total," + (Date.now()-start_global_g));
//		Status.concat("Failed to download enough fragments for data recovery<br>");
		document.getElementById("submit_status").innerHTML = "Download failed";
		download_complete = 1;
		error_requests = 0;
		return;
	    }
	    if ( remaining_shares_g > 0 )
		downloadNextShare();
	});
    request.send();
};
downloadData = function()
{
    start_g = Date.now();
    remaining_shares_g = num_shares_g;

    // HTMLUNIT
//    alert("Downloading...");
//    Status.concat("Downloading...<br>");
    for ( i=0; i<MAX_ACTIVE_REQUESTS && remaining_shares_g>0; i++ )
	downloadNextShare();
};

// returns decoded value of 'data' based on 'encoding'
decodeData = function(data)
{
    // HTMLUNIT
    start_g = Date.now();
    var dec = "aoeu";
    switch(encoding_g)
    {
    case "NONE":
	dec = new String(data);
	break;
    case "AES":
	dec = decryptAES(data, aes_key_g);
	break;
    case "SSS":
	dec = shamirsSecretRecover(data, threshold_g);
	break;
    case "XOR":
	dec = xorSecretRecover(data, num_shares_g);
	break;
    default:
	alert("Invalid encoding type: ".concat(encoding));
    }
    // HTMLUNIT
    alert("Decoding," + (Date.now()-start_g));
    return dec;
}

//Status.disable();

main = function ()
{
    start_global_g = Date.now();
    document.getElementById("decoded_data_download_link").innerHTML = "";

    var bucket_name_input = document.main_form.bucket_name_in,
    obj_name_input = document.main_form.obj_name_in,
    user_name_input = document.main_form.user_name_in,
    password_input = document.main_form.password_in;
    // HTMLUNIT
//    cred_input = document.main_form.cred_in;

    if ( user_name_input.value.length == 0 )
    {
	alert("User name required");
	return false;
    }
    user_name_g = user_name_input.value;
    if ( password_input.value.length == 0 )
    {
	alert("Password required");
	return false;
    }
    password_hash_g = CryptoJS.SHA1(password_input.value);
    bucket_name_g = bucket_name_input.value;
    if ( obj_name_input.value.length == 0 )
    {
	alert("Object name required");
	return false;
    }
    if ( obj_name_input.value.length == 0 )
    {
	alert("Object name required");
	return false;
    }
    obj_name_g = obj_name_input.value;
    // HTMLUNIT
//    if ( !cred_input.files || cred_input.files.length == 0 )
//    {
//	alert("Credentials file required");
//	return false;
//    }
//    cred_file_g = cred_input.files[0];

    // TODO: how to get source region
    src_reg_g = "use1";

    data_g = [];

    active_requests_g = 0;
    error_requests_g = 0;
    remaining_shares_g = 0;
    download_complete = 0;

    // HTMLUNIT: hard code in credentials
//    importCred(cred_file_g);
    AWS.config.update({
	accessKeyId: 'AKIAI7BF5BGNS2USENSA',
	secretAccessKey: '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'
    });

    var ret = requestMetadata(user_name_g,password_hash_g,
			      bucket_name_g, obj_name_g, src_reg_g);
    placements_g = ret[0];
    encoding_g = ret[1];
    threshold_g = ret[2];
    num_shares_g = ret[3];
    error = ret[4];
    
    if ( error != 0 )
    {
	Status.concat(ServerError.parse(error));
	return false;
    }

    // HTMLUNIT
    downloadData();
//    Manager.wait("importCred",downloadData,"");
    
    return true;
}
document.getElementById('submit_button').addEventListener('click',main);

Status.concat("Download initializations complete.<br>");
