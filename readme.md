recreational http server stub using kqueue that sends a hard-coded index.html. <br>
supports tens of thousands of roughly concurrent requests depending on hardware and os settings. <br>
well, more like ten-ish thousand concurrent connections, depending on file-descriptor limits of the OS. <br>
can be optimized.<br>
code needs some cleaning up, especially in terms of error handling