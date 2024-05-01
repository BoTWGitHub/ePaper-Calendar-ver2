<?php
if ($_SERVER["REQUEST_METHOD"] == "POST") {
    $ssid = $_POST['ssid'];
    $password = $_POST['password'];
    $command = "sudo connect_and_set_default.sh $ssid $password";
    exec($command, $output, $return_var);
    if ($return_var == 0) {
        echo "Connected successfully to " . $_POST['ssid'];
    } else {
        echo "Failed to connect.";
    }
}
?>
<form method="post">
    SSID: <input type="text" name="ssid" required><br>
    Password: <input type="password" name="password" required><br>
    <input type="submit" value="Connect">
</form>
