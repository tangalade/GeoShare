var AESJsonFormatter = {
    stringify: function (cipherParams) {
        // create json object with ciphertext
        var jsonObj = {
            ct: cipherParams.ciphertext.toString(CryptoJS.enc.Base64)
        };

        // optionally add iv and salt
        if (cipherParams.iv) {
            jsonObj.iv = cipherParams.iv.toString();
        }
        if (cipherParams.salt) {
            jsonObj.s = cipherParams.salt.toString();
        }

        // stringify json object
        return JSON.stringify(jsonObj);
    },

    parse: function (jsonStr) {
        // parse json string
        var jsonObj = JSON.parse(jsonStr);

        // extract ciphertext from json object, and create cipher params object
        var cipherParams = CryptoJS.lib.CipherParams.create({
            ciphertext: CryptoJS.enc.Base64.parse(jsonObj.ct)
        });

        // optionally extract iv and salt
        if (jsonObj.iv) {
            cipherParams.iv = CryptoJS.enc.Hex.parse(jsonObj.iv)
        }
        if (jsonObj.s) {
            cipherParams.salt = CryptoJS.enc.Hex.parse(jsonObj.s)
        }

        return cipherParams;
    }
};

// Use the JSON formatter to convert the CipherParams object into a string
encryptAES = function (plaintext, key)
{
    return CryptoJS.AES.encrypt(plaintext, key, { format: AESJsonFormatter }).toString();
};
// Use the JSON formatter to convert the string into a CipherParams object
decryptAES = function (ciphertext, key)
{
    return CryptoJS.AES.decrypt(ciphertext, key, { format: AESJsonFormatter })
	.toString(CryptoJS.enc.Utf8);
};

/*
 * Backup plan: manually creating a CryptoJS.lib.CipherParams object works
 *   must save the salt and ciphertext, IV will be created from the key
 */
//var params = CryptoJS.AES.encrypt(data, aes_key_g);
//var ciphertext = params.ciphertext.toString(CryptoJS.enc.Base64);
//var salt = params.salt.toString(CryptoJS.enc.Hex);
//alert(ciphertext);
//var newParams = CryptoJS.lib.CipherParams.create({
//    ciphertext: CryptoJS.enc.Base64.parse(ciphertext),
//    salt: CryptoJS.enc.Hex.parse(salt)
//});
//var plaintext = CryptoJS.AES.decrypt(newParams, aes_key_g).toString(CryptoJS.enc.Utf8);
