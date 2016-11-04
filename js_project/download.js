var user_name_g, password_hash_g;
var bucket_name_g, obj_name_g;
var placements_g;
var data_g;

var encoding_g;
var num_shares_g = 5, threshold_g = 3, num_replicas_g = 1;
var src_reg_g = "use1";

// maximum concurrent requests
var MAX_ACTIVE_REQUESTS = 8;
// how many requests are currently being processed
var active_requests_g = 0;
// how many completed requests had errors
var error_requests_g = 0;
// how many shares do not have any running requests
var remaining_shares_g = 0;
var download_complete = 0;

function updateSource(position)
{
    src_reg_g = closestRegion(position.coords);
    Status.concat("Source region chosen by geolocation: " + src_reg_g + "<br>");
    Manager.complete("getPosition");
}

// user, obj_name, scheme, num_shares, thresh, excluded=use1,euw1, src_reg
// only works with IE7+
requestMetadata = function(user_name, password_hash,
			   bucket_name, obj_name, src_reg)
{
    var placements = [];
    var replicas = [];
    var keys = [];
    var scheme, threshold=3, num_shares=5, num_replicas=1, error;
    xmlhttp = new XMLHttpRequest();

    var url = "http://" + server_address_g +
	"/"             + download_prefix_g +
	"?user="        + user_name +
	"&pass="        + password_hash +
	"&bucketname="  + bucket_name +
	"&objname="     + obj_name +
	"&src_reg="     + src_reg;
//    Status.concat(url + "<br>");

    // set to false if synchronous, but then you are stuck if server isn't running or something goes wrong with request
    xmlhttp.open("GET",url,false);
    xmlhttp.send();

//    Status.concat("Received response: " + xmlhttp.responseText + "<br>");

    var jsonObj = JSON.parse(xmlhttp.responseText);
    scheme = jsonObj.scheme;
    var params = jsonObj.params;
    var params_arr = params.split("-");
    if ( params_arr.length > 0 )
	threshold = params_arr[0];
    if ( params_arr.length > 1 )
	num_shares = params_arr[1];
    if ( params_arr.length > 2 )
	num_replicas = params_arr[2];

    if ( jsonObj.replicas )
	for ( i=0; i<jsonObj.replicas.length; i++ )
    	    replicas[i] = {bucket: jsonObj.replicas[i].aws_bucket,
    			     name: jsonObj.replicas[i].aws_name};
    if ( jsonObj.keys )
	for ( i=0; i<jsonObj.keys.length; i++ )
    	    keys[i] = {bucket: jsonObj.keys[i].aws_bucket,
    			     name: jsonObj.keys[i].aws_name};

    var latitude, longitude;
    var min_reg, min_rep;
    var min_dist = Number.MAX_VALUE;

    // get lat/long for source region
    for ( i=0; i<regions_g.length; i++ )
    {
	if ( regions_g[i].name == src_reg_g )
	{
	    latitude = regions_g[i].latitude;
	    longitude = regions_g[i].longitude;
	}
    }
    // find closest replica and push onto placements
    for ( i=0; i<regions_g.length; i++ )
    {
	var exists = false;
	var j;
	for ( j=0; j<replicas.length; j++ )
	{
	    if ( replicas[j].bucket == regions_g[i].bucket )
	    {
		exists = true;
		break;
	    }
	}
	if ( !exists )
	    continue;

	var dist = Math.sqrt((Math.pow(regions_g[i].latitude-latitude,2)
			      + Math.pow(regions_g[i].longitude-longitude,2)));
	if ( min_dist > dist )
	{
	    min_reg = i;
	    min_dist = dist;
	    min_rep = j;
	}
    }
    placements.push(replicas[min_rep]);
    for ( i=0; i<keys.length; i++ )
	placements.push(keys[i]);
    
    error = jsonObj.error;

    return [placements, scheme, threshold, num_shares, num_replicas, error];
};

downloadNextShare = function()
{
    if ( remaining_shares_g == 0 )
	return;
    var idx = placements_g.length - remaining_shares_g;
    var bucket = new AWS.S3({
	params: {Bucket: placements_g[idx].bucket},
    });
    var params = {
	Key: placements_g[idx].name,
    };
    var request = bucket.getObject(params);
    active_requests_g++;
    remaining_shares_g--;
    Status.concat("&nbsp;&nbsp;".concat(idx+1,": ",placements_g[idx].bucket,"/",placements_g[idx].name,"<br>"));
    request.
	on('success', function(resp) {
	    if ( download_complete )
		return;
	    active_requests_g--;
	    // TODO: hacks to make it work for AES-SSS, only stops when all shares are done since there is no differentiation between the replica and the key shares
	    data_g[idx] = resp.data.Body.toString();
	    if ( (active_requests_g == 0) && (remaining_shares_g == 0) )
	    {
//	    if ( num_shares_g - remaining_shares_g - active_requests_g - error_requests_g >= threshold_g )
//	    {
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
    remaining_shares_g = placements_g.length;

    Status.concat("Downloading...<br>");
    for ( i=0; i<MAX_ACTIVE_REQUESTS && remaining_shares_g>0; i++ )
	downloadNextShare();
};

// returns decoded value of 'data' based on 'encoding'
//   'key' is used if needed
decodeData = function(data)
{
//    start_g = Date.now();
    var dec = "";
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
    case "AES-SSS":
	dec_key = shamirsSecretRecover(data.slice(1), threshold_g);
	dec = decryptAES(data[0], dec_key);
	break;
    case "XOR":
	dec = xorSecretRecover(data, num_shares_g);
	break;
    default:
	alert("Invalid encoding type: ".concat(encoding));
    }
//    Status.concat("Decoding time: " + (Date.now()-start_g) + "ms<br>");
    return dec;
}

//Status.disable();

main = function ()
{
    Status.update("");
    document.getElementById("decoded_data_download_link").innerHTML = "";

    var bucket_name_input = document.main_form.bucket_name_in,
    obj_name_input = document.main_form.obj_name_in,
    user_name_input = document.main_form.user_name_in,
    password_input = document.main_form.password_in,
    cred_input = document.main_form.cred_in;

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
    Manager.wait("getPosition",importCred,cred_file_g);

    var ret = requestMetadata(user_name_g,password_hash_g,
			      bucket_name_g, obj_name_g, src_reg_g);
    placements_g = ret[0];
    encoding_g = ret[1];
    threshold_g = ret[2];
    num_shares_g = ret[3];
    num_replicas_g = ret[4];
    error = ret[5];
    
    switch(error)
    {
    case 0:
	break;
    case 1:
	Status.concat("Invalid username/password combination." + "<br>");
	return false;
    case 2:
	Status.concat("You do not have permission to modify this object." + "<br>");
	return false;
    default:
	Status.concat("Unknown error received: " + error + "<br>");
	return false;
    }
	
    Manager.wait("importCred",downloadData,"");
    //downloadData();
    
    return true;
}
document.getElementById('submit_button').addEventListener('click',main);

getPosition(updateSource, errPosition);

Status.concat("Download initializations complete.<br>");