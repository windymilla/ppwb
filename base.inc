<?php
// This file is included at the top of every page. It sets up some common
// infrastructure and defines some common functions.

// Handle exceptions in a user-friendly manner
set_exception_handler('exception_handler');

//===========================================================================
// Configuration

// Define some global configuration values. These can be overridden in
// an optional configuration file (config.php).

// $base_workdir is where files are uploaded and where results are stored.
// It must be writeable by the webserver and also needs to be accessible by
// URL ($base_workurl). $base_workurl can be a relative or absolute URL to
// where the workfiles can be accessed.
global $base_workdir, $base_workurl;
$base_workdir = "./t";
$base_workurl = "./t";

// $python_runner defines the python binary, or proxy script, to run the
// python-based tools. See README.md for more information on setting up
// the python tools.
global $python_runner;
$python_runner = "python3";

// URLs for help and documentation links.
global $help_url, $docs_url;
$help_url = "https://www.pgdp.net/phpBB3/viewtopic.php?f=13&t=64838";
$docs_url = "https://www.pgdp.net/wiki/DP_Official_Documentation:PP_and_PPV/Post-Processing_Workbench";

// $maint_ips specifies IP addresses that are allowed to call the maintenance
// scripts, beyond localhost.
global $maint_ips;
$maint_ips = [];

// $maint_days_ago specifies how many days of files to keep when the
// maintenance script runs. Files older than this many days will be deleted.
global $maint_days_ago;
$maint_days_ago = 14;

// If defined, $alert_message will be shown on all pages.
global $alert_message;
$alert_message = "";

// Load an (optional) configuration file
if(is_file("config.php")) {
    include("config.php");
}


//===========================================================================
// Common functions used by many/all pages

function exception_handler($exception)
{
    echo "Error: " . htmlspecialchars($exception->getMessage());
}

// Output valid HTML page header
function output_header($header="", $addl_links=[], $js="")
{
    global $alert_message;

    if($header)
    {
        $title = "PP Workbench: $header";
    }
    else
    {
        $title = "Post-Processing Workbench";
        $header = "pp workbench";
    }

    echo <<<HEAD
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name=viewport content="width=device-width, initial-scale=1">
    <title>$title</title>
    <link rel="stylesheet" type="text/css" href="ppwb.css">
    <script src="https://code.jquery.com/jquery-1.9.1.min.js"></script>
    <script>$js</script>
  </head>
  <body>
  <div class="alert">$alert_message</div>
  <div id="header" class='hsty'>$header</div>
	<hr style='border:none; border-bottom:1px solid silver;'>  
HEAD;

    // Register a shutdown callback to always close out the page
    register_shutdown_function('output_footer', $addl_links);
}

// You should not call this function directly as it is registered as a
// shutdown callback with output_header() to ensure that the HTML is
// always closed properly.
function output_footer($addl_links=[])
{
    global $docs_url, $help_url;

    // prepend a return to the main page
    if(basename($_SERVER["PHP_SELF"]) != "index.php")
    {
        $addl_links = array_merge(["index.php" => "MAIN PAGE"], $addl_links);
    }

    function make_links($addl_links) {
        $links = [];
        foreach($addl_links as $url => $name)
        {
            $links[] = "<a style='font-size: 70%' href='$url'>$name</a>";
        }
        return join("&nbsp;|&nbsp;", $links);
    }

    $left_links = make_links($addl_links);
    $right_links = make_links([
        "https://github.com/DistributedProofreaders/ppwb" => "GITHUB",
        $docs_url => "DOCS",
        $help_url => "HELP",
    ]);

    echo <<<FOOT
  <div id="footer">
    <hr style='border:none; border-bottom:1px solid silver;'>
    <table style='width: 100%'>
      <tr>
        <td style='text-align: left'>$left_links</td>
        <td style='text-align: right'>$right_links</td>
      </tr>
    </table>
  </div>
  </body>
</html>
FOOT;
}

// initialize a working directory to store uploaded and processed files
function init_workdir()
{
    global $base_workdir, $base_workurl;

    $upid = uniqid('r');

    $workdir = "$base_workdir/$upid";
    mkdir($workdir, 0755);

    $workurl = "$base_workurl/$upid";

    return [$workdir, $workurl, $upid];
}

// get user's IP address
function getUserIP()
{
    if (array_key_exists('HTTP_X_FORWARDED_FOR', $_SERVER) && !empty($_SERVER['HTTP_X_FORWARDED_FOR'])) {
        if (strpos($_SERVER['HTTP_X_FORWARDED_FOR'], ',') > 0) {
            $addr = explode(",", $_SERVER['HTTP_X_FORWARDED_FOR']);
            return trim($addr[0]);
        }
        else {
            return $_SERVER['HTTP_X_FORWARDED_FOR'];
        }
    }
    else {
        return $_SERVER['REMOTE_ADDR'];
    }
}

// log tool access
function log_tool_access($tool, $upid)
{
    global $base_workdir;

    // make a record of this attempted run ---
    // format is:
    //    2019-03-31 12:46:44 pptext r5ca0b6b499bec 67.161.200.103
    $ip = getUserIP();
    $s = sprintf("%s %6s %s %s\n", date('Y-m-d H:i:s'), $tool, $upid, $ip);
    file_put_contents("$base_workdir/access.log", $s, FILE_APPEND);
}

function log_tool_action($workdir, $label, $message)
{
    file_put_contents("$workdir/messages.log", "$label: $message\n", FILE_APPEND);
}

class UploadError extends Exception {}

function process_file_upload($formid, $workdir, $allowed_extensions=[])
{
    if(!isset($_FILES[$formid]) || !$_FILES[$formid]["name"]) {
        throw new UploadError("No file was uploaded");
    }

    // get the information about the file
    $file_name = $_FILES[$formid]['name'];
    $file_size = $_FILES[$formid]['size'];
    $file_tmp = $_FILES[$formid]['tmp_name'];
    $file_type = $_FILES[$formid]['type'];

    // lowercase the extension
    $file_ext = strtolower(pathinfo($file_name, PATHINFO_EXTENSION));

    // restrict basename to alphanumeric, -, or _ after replacing spaces
    // with _
    $file_basename = pathinfo($file_name, PATHINFO_FILENAME);
    $file_basename = preg_replace("/\s+/", "_", $file_basename);
    $file_basename = preg_replace("/[^-_a-zA-Z0-9]/", "", $file_basename);
    if(!$file_basename) {
        throw new UploadError("Filename did not contain any valid characters");
    }

    $final_filepath = "$workdir/$file_basename.$file_ext";
    if(move_uploaded_file($file_tmp, $final_filepath) === FALSE) {
        throw new UploadError("error moving uploaded file to working directory");
    }

    // begin a series of validation tests
    try {
        // does it pass the anti-virus tests?
        $av_test_result = array();
        $av_retval = 0;
        $cmd = "/usr/bin/clamdscan '" . escapeshellcmd($final_filepath) . "'";
        exec($cmd, $av_test_result, $av_retval);
        if ($av_retval == 1) {
            throw new UploadError("file rejected by AV scanner");
        }
        if ($av_retval != 0) {
            throw new UploadError("error running AV scanner");
        }

        // was a file uploaded?
        if ($_FILES[$formid]['size'] == 0) {
            throw new UploadError("no file was uploaded");
        }

        // do they claim it's an allowed type?
        if ($allowed_extensions && in_array($file_ext, $allowed_extensions) === false) {
            throw new UploadError(
                sprintf("file must have a %s extension", join(" or ", $allowed_extensions))
            );
        }
    } catch(Exception $e) {
        unlink($final_filepath);
        rmdir($workdir);
        throw $e;
    }

    log_tool_action($workdir, "uploaded file", $final_filepath);

    return $final_filepath;
}

// If a text file is ISO-8859 convert it to UTF-8
// Returns true if a conversion was necessary, else false
function ensure_utf8_file($filename) {
    $cmd = sprintf("file %s", escapeshellarg($filename));
    exec($cmd, $ppf_output, $ppf_exitcode);
    if (strpos($ppf_output[0], "ISO-8859") !== false) {
        // Latin-1. convert to UTF-8
        $tmpfname = "$filename.tmp";
        $cmd = sprintf("iconv -f ISO_8859-1 -t UTF-8 -o %s %s",
            escapeshellarg($tmpfname), escapeshellarg($filename)
        );
        exec(escapeshellcmd($cmd), $ppf_output, $ppf_exitcode);
        rename($tmpfname, $filename);
        log_tool_action(dirname($filename), "UTF-8 conversion", "$filename converted from ISO-8859-1");
        return true;
    }
    return false;
}

// from https://stackoverflow.com/questions/2320608/php-stderr-after-exec
function run_command($cmd, &$stdout=null, &$stderr=null) {
    $proc = proc_open($cmd, [
        1 => ['pipe','w'],
        2 => ['pipe','w'],
    ], $pipes);
    $stdout = stream_get_contents($pipes[1]);
    fclose($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[2]);
    return proc_close($proc);
}
