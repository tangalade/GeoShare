var OBJ_NAME_VALID_REGEX = /^[0-9a-zA-Z!\-_.*'()\/]+$/;
var USER_NAME_VALID_REGEX = /^[0-9a-zA-Z_\-]+$/;

var server_address_g = "52.0.143.176:8000";
var upload_prefix_g = "info";
var download_prefix_g = "get";
var share_prefix_g = "share";
var create_account_prefix_g = "new";
var change_password_prefix_g = "change";

// defunct, used in original upload.js and download.js
var buckets_g = [
    "aws-test-use1",
    "aws-test-usw1",
    "aws-test-usw2",
    "aws-test-euw1",
    "aws-test-apne1",
    "aws-test-apse1",
    "aws-test-apse2",
    "aws-test-sae1",
];

var regions_g = [
    {
	name: "use1",
	latitude: 37.223232,
	longitude: -81.449872,
	bucket: "aws-test-use1",
    },
    {
	name: "usw1",
	latitude: 38.837522,
	longitude: -120.895824,
	bucket: "aws-test-usw1",
    },
    {
	name: "usw2",
	latitude: 43.804133,
	longitude: -120.554201,
	bucket: "aws-test-usw2",
    },
    {
	name: "euw1",
	latitude: 53.412910,
	longitude: -8.243890,
	bucket: "aws-test-euw1",
    },
    {
	name: "sae1",
	latitude: -23.550520,
	longitude: -46.633309,
	bucket: "aws-test-sae1",
    },
    {
	name: "apne1",
	latitude: 35.689487,
	longitude: 139.691706,
	bucket: "aws-test-apne1",
    },
    {
	name: "apse1",
	latitude: 1.352083,
	longitude: 103.819836,
	bucket: "aws-test-apse1",
    },
    {
	name: "apse2",
	latitude: -33.867487,
	longitude: 151.206990,
	bucket: "aws-test-apse2",
    }
];

var aes_key_g = "aoesunaethousaock;";
var cred_file_g;

var start_g;

//AWS.config.update({
//    accessKeyId: 'AKIAI7BF5BGNS2USENSA',
//    secretAccessKey: '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'
//});
//AWS.config.region = 'ap-northeast-1';

// manages the <div> tag with id "js_status_window"
// not enabled by default
var Status = {
    status: "",
    // will not display anything if false
    disabled: false,
    enable: function()
    {
	this.disabled = false;
    },
    // disable status window and erases anything in it
    disable: function()
    {
	this.disabled = true;
	this.status = "";
	document.getElementById("js_status_window").innerHTML = "";
    },
    update: function(text)
    {
	if ( this.disabled )
	    return;
	this.status = text;
	document.getElementById("js_status_window").innerHTML = text;
    },
    // appends arbitrary text to any existing text
    concat: function(text)
    {
	var new_text;
	if ( this.disabled )
	    return;
	if ( this.status )
	    new_text = this.status.concat(text);
	else
	    new_text = text;
	this.status = new_text;
	document.getElementById("js_status_window").innerHTML = new_text;
    }
};

var Manager = {
    disabled: false,
    status: [],
    callbacks: [],
    parameters: [],
    // calls any callbacks for this completed action
    complete: function(action)
    {
	this.status[action] = true;
	if ( action in this.callbacks )
	{
	    for ( i=0; i<this.callbacks[action].length; i++ )
	    {
		this.callbacks[action][i](this.parameters[action][i]);
	    }
	    this.callbacks[action] = [];
	    this.parameters[action] = [];
	}
    },
    // if action has completed, call callback, else add it to callbacks
    wait: function(action, callback, parameter)
    {
	if ( this.status[action] == true )
	{
	    callback(parameter);
	}
	else
	{
	    if ( !(action in this.callbacks) )
	    {
		this.callbacks[action] = [];
		this.parameters[action] = [];
	    }
	    this.callbacks[action].push(callback);
	    this.parameters[action].push(parameter);
	}
    }
}
processCSV = function(allText)
{
    var allTextLines = allText.split(/\r\n|\n/);
    var headers = allTextLines[0].split(',');
    var lines = [];

    for (var i=1; i<allTextLines.length; i++) {
        var data = allTextLines[i].split(/,/);
        if (data.length == headers.length) {

            var tarr = [];
            for (var j=0; j<headers.length; j++) {
                tarr[headers[j]] = data[j];
            }
            lines.push(tarr);
        }
    }
    return lines;
};

importCred = function(file, next_func, next_func_param)
{
    var reader = new FileReader();
    reader.onload = (function(file_ref) {
	creds = processCSV(reader.result);
	if ( !("Access Key Id" in creds[0]) ||
	     (creds[0]["Access Key Id"].length < 16) ||
	     (creds[0]["Access Key Id"].length > 32) )
	{
	    alert("Invalid Access Key Id");
	    return false;
	}
	if ( !("Secret Access Key" in creds[0]) ||
	     (creds[0]["Secret Access Key"].length < 1) )
	{
	    alert("Invalid Secret Access Key");
	    return false;
	}
	AWS.config.update({
	    accessKeyId: creds[0]["Access Key Id"],
	    secretAccessKey: creds[0]["Secret Access Key"]
	});
	Manager.complete("importCred");
    });
    reader.readAsBinaryString(file);
};

function closestRegion(coords)
{
    var latitude = coords.latitude;
    var longitude = coords.longitude;
    var min_reg = regions_g[0];
    var min_dist = Math.sqrt((Math.pow(min_reg.latitude-latitude,2)
			      + Math.pow(min_reg.longitude-longitude,2)));
    for ( i=1; i<regions_g.length; i++ )
    {
	var dist = Math.sqrt((Math.pow(regions_g[i].latitude-latitude,2)
			      + Math.pow(regions_g[i].longitude-longitude,2)));
	if ( min_dist > dist )
	{
	    min_reg = regions_g[i];
	    min_dist = dist;
	}
    }
    return min_reg.name;
};

function getPosition(goodPos, badPos)
{
    if ( navigator.geolocation )
	navigator.geolocation.getCurrentPosition(goodPos, badPos);
    else
    {
	Status.concat("Geolocation is not supported by this browser, ")
	Status.concat("defaulting to source use1");
	Manager.complete("getPosition");
    }
}

function errPosition(error)
{
    switch(error.code) {
    case error.PERMISSION_DENIED:
        Status.concat("User denied the request for geolocation, ");
        break;
    case error.POSITION_UNAVAILABLE:
        Status.concat("Location information is unavailable, ");
        break;
    case error.TIMEOUT:
        Status.concat("The request to get user location timed out, ");
        break;
    case error.UNKNOWN_ERROR:
        Status.concat("An unknown error occurred, ");
        break;
    }
    Status.concat("defaulting to source use1<br>");
    Manager.complete("getPosition");
}

var genLabel = function(name)
{
    return name + "_label";
};

// Generic HTML element modifications, accessed by ID
// NOTE: if a prev property is a boolean value, even if it is defined, it could be false, so it may keep setting the prev value to the current value
//   make sure you check with typeof() === 'undefined'
var Elem = {
    prev: {},
    fuzz: function(name)
    {
	var elem = document.getElementById(name);
	var label = document.getElementById(genLabel(name));
	var css = window.getComputedStyle(elem, null);
	var css_l = window.getComputedStyle(label, null);
	if ( typeof(this.prev[name + "disabled"]) === 'undefined' )
	    this.prev[name + "disabled"] = elem.disabled;
	this.prev[name + "opacity"] = this.prev[name + "opacity"] || css.opacity;
	this.prev[genLabel(name) + "opacity"] = this.prev[genLabel(name) + "opacity"] || css_l.opacity;
	elem.disabled = true;
	elem.style.opacity = '0.5';
	label.style.opacity = '0.5';
    },
    unFuzz: function(name)
    {
	document.getElementById(name).disabled = this.prev[name + "disabled"];
	document.getElementById(name).style.opacity = this.prev[name + "opacity"];
	document.getElementById(genLabel(name)).style.opacity = this.prev[genLabel(name) + "opacity"];
    },
    error: function(name)
    {
	var elem = document.getElementById(name);
	var label = document.getElementById(genLabel(name));
	var css = window.getComputedStyle(elem, null);
	var css_l = window.getComputedStyle(label, null);
	this.prev[name + "backgroundColor"] = this.prev[name + "backgroundColor"] || css.backgroundColor;
	this.prev[name + "borderColor"] = this.prev[name + "borderColor"] || css.borderColor;
	this.prev[name + "color"] = this.prev[name + "color"] || css.color;
	this.prev[name + "fontWeight"] = this.prev[name + "fontWeight"] || css.fontWeight;
	this.prev[genLabel(name) + "color"] = this.prev[genLabel(name) + "color"] || css_l.color;
	elem.style.backgroundColor = 'LightCoral';
	elem.style.borderColor = 'Red';
	elem.style.color = 'Red';
	elem.style.fontWeight = 'Bold';
	label.style.color = 'Red';
    },
    unError: function(name)
    {
	document.getElementById(name).style.backgroundColor = this.prev[name + "backgroundColor"];
	document.getElementById(name).style.borderColor = this.prev[name + "borderColor"];
	document.getElementById(name).style.color = this.prev[name + "color"];
	document.getElementById(name).style.fontWeight = this.prev[name + "fontWeight"];
	document.getElementById(genLabel(name)).style.color = this.prev[genLabel(name) + "color"];
    },
    hide: function(name)
    {
	document.getElementById(name).disabled = true;
	document.getElementById(name).style.visibility = 'hidden';
	document.getElementById(name.concat("_label")).style.visibility = 'hidden';
    },
    show: function(name)
    {
	document.getElementById(name).disabled = false;
	document.getElementById(name).style.visibility = 'visible';
	document.getElementById(name.concat("_label")).style.visibility = 'visible';
    }
}

if ( document.getElementById('user_name_in') )
{
userNameFormUpdate = function()
{
    var user_name_input = document.getElementById("user_name_in");
    var user_name = user_name_input.value;
    
    if ( user_name.length < 1 || USER_NAME_VALID_REGEX.test(user_name) )
	Elem.unError("user_name_in");
    else
	Elem.error("user_name_in");
};
document.getElementById("user_name_in").addEventListener('change',userNameFormUpdate);
document.getElementById("user_name_in").addEventListener('input',userNameFormUpdate);
userNameFormUpdate();
}

if ( document.getElementById('obj_name_in') )
{
objNameFormUpdate = function()
{
    var obj_name_input = document.getElementById("obj_name_in");
    var obj_name = obj_name_input.value;
    
    if ( obj_name.length < 1 || OBJ_NAME_VALID_REGEX.test(obj_name) )
	Elem.unError('obj_name_in');
    else
	Elem.error('obj_name_in');
};
document.getElementById('obj_name_in').addEventListener('change',objNameFormUpdate);
document.getElementById('obj_name_in').addEventListener('input',objNameFormUpdate);
objNameFormUpdate();
}

Status.update("Generic initializations complete.<br>");
// Okay so several options to start working on:
//   make user interface look pretty and change dynamically based on their selections
//   read more on REST stuff and plan naming scheme to comm with Doug's algorithm
//   find libraries and figure out how to make the GET requests to Doug's algorithm
//   implement the user object download, maybe a list function integrated with the user interface?
// don't forget drag-and-drop file upload if you end up doing this for real
