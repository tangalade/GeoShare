var OBJ_NAME_VALID_REGEX = /^[0-9a-zA-Z!\-_.*'()\/]+$/;
var USER_NAME_VALID_REGEX = /^[0-9a-zA-Z_\-]+$/;

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

var aes_key_g = "aoesunaethousaock;";

AWS.config.update({
    accessKeyId: 'AKIAI7BF5BGNS2USENSA',
    secretAccessKey: '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'
});
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
	    new_text = status.concat(text);
	else
	    new_text = text;
	this.status = new_text;
	document.getElementById("js_status_window").innerHTML = new_text;
    }
};
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

// This assumes obj_name_in element exists, and that it operates under the same restrictions as AWS S3 object names
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

Status.update("Generic initializations complete.<br>");
// Okay so several options to start working on:
//   make user interface look pretty and change dynamically based on their selections
//   read more on REST stuff and plan naming scheme to comm with Doug's algorithm
//   find libraries and figure out how to make the GET requests to Doug's algorithm
//   implement the user object download, maybe a list function integrated with the user interface?
// don't forget drag-and-drop file upload if you end up doing this for real
