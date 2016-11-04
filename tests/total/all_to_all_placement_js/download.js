var user_name_g, password_hash_g, obj_name_g;
var placement_g;
var data_g;

var encoding_g;
var num_shares_g = 5, threshold_g = 3;

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
requestMetadata = function(user_name, password_hash, obj_name)
{
    var placement = [];
    var threshold, num_shares, scheme;

    for ( i=0; i<num_shares_g; i++ )
	placement[i] = {bucket: buckets_g[0],
			name: obj_name.concat("-",i)};
    return placement;
};

downloadNextShare = function()
{
    if ( remaining_shares_g == 0 )
	return;
    var idx = num_shares_g - remaining_shares_g;
    var bucket = new AWS.S3({
	params: {Bucket: placement_g[idx].bucket},
    });
    var params = {
	Key: placement_g[idx].name,
    };
    var request = bucket.getObject(params);
    active_requests_g++;
    remaining_shares_g--;
    Status.concat("&nbsp;&nbsp;".concat(idx+1,": ",placement_g[idx].bucket,"/",placement_g[idx].name,"<br>"));
    request.
	on('success', function(resp) {
	    if ( download_complete )
		return;
	    active_requests_g--;
	    data_g[data_g.length] = resp.data.Body.toString();
	    if ( num_shares_g - remaining_shares_g - active_requests_g - error_requests_g >= threshold_g )
	    {
		Status.concat("Finished downloading, " + error_requests_g + " failed requests<br>");
		download_complete = 1;
		var plain = decodeData(data_g);
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
		document.getElementById("download_status").innerHTML = "Download failed";
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
		Status.concat("Finished downloading, " + error_requests_g + " failed requests<br>");
		download_complete = 1;
		var plain = decodeData(data_g);
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
		Status.concat("Failed to download enough fragments for data recovery<br>");
		document.getElementById("download_status").innerHTML = "Download failed";
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
    remaining_shares_g = num_shares_g;

    Status.concat("Downloading...<br>");
    for ( i=0; i<MAX_ACTIVE_REQUESTS && remaining_shares_g>0; i++ )
	downloadNextShare();
};

// returns decoded value of 'data' based on 'encoding'
decodeData = function(data)
{
    start_g = Date.now();
    var dec = "aoeu";
    switch(encoding_g)
    {
    case "NONE":
	dec = data;
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
    Status.concat("Decoding time: " + (Date.now()-start_g) + "ms<br>");
    return dec;
}

//Status.disable();

mainDownload = function ()
{
    var obj_name_input = document.upload.obj_name_in,
    user_name_input = document.upload.user_name_in,
    password_input = document.upload.password_in,
    cred_input = document.upload.cred_in;

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
    if ( obj_name_input.value.length == 0 )
    {
	alert("Object name required");
	return false;
    }
    obj_name_g = obj_name_input.value;
    if ( cred_input.files.length == 0 )
    {
	alert("Credentials csv required");
	//Status.concat("using default credentials" + "<br>");
	return false;
    }
    cred_file_g = cred_input.files[0];

    data_g = [];

    active_requests_g = 0;
    error_requests_g = 0;
    remaining_shares_g = 0;
    download_complete = 0;

    importCred(cred_file_g);

    // these will all come from requestMetadata eventually
    threshold_g = 3;
    num_shares_g = 5;
    encoding_g = "SSS";
    placement_g = requestMetadata(user_name_g,password_hash_g,obj_name_g);
    
    Manager.wait("importCred",downloadData,"");
    //downloadData();
    
    return true;
}
document.getElementById('download_button').addEventListener('click',mainDownload);

Status.concat("Download initializations complete.<br>");
