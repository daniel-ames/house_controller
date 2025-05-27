House controller code that runs on the optiplex.

Some notes, cuz you gon come in here to do something and be like "duuuhhh??? whaaahh???"

email notifications - optiplex emails you via msmtp. I chose msmtp cuz arch aint got no sendmail package. Not thru pacman anyway.
So I followed this (https://wiki.archlinux.org/title/Msmtp) to setup msmtp.
See optiplex--> ~/.msmtprc for the config I landed on.
Yes, I used a system() call to call sendmail (smylinked to msmtp). I tried to use libesmtp with some success, but it kept segfaulting when trying to send more than one email per process.
I opened an issue with libesmtp, they didn't know what was wrong. It was crashing in SSH_New() of libssl.so.3. I poked it with a stick for a while. Gave up. Aint nobody got time for dat.
Switched to just calling sendmail (msmtp actually).

gmail accout - ameshousecontroller, pw is on the super secure sticky on the optiplex
I had to create an "app password" so the mta on arch can login and send emails.
To do this, I had to create the ameshousecontroller account, then set up 2fa (it won't let you do an app pw unless 2fa is set up first).
Then manually go to https://myaccount.google.com/apppasswords to create the app pw. (I did this for the fishtank controller too back in the day. I must have.)


As of this writing (May 24, 2025), house controller only collects data from sewage_pump. See that repo for the sketch.

For the graphing to work, you need gnuplot and unix2dos (which is in the dos2unix package).
A note about pacman on arch - it gave me a hassle cuz it couldn't find some dependencies for gnuplot. Long story short, it was looking for a package that was deprecated and had been pulled from the mirror. Run "pacman -Syu" to make it fetch the latest db. That fixed the broken dependency and allowed me to install gnuplot.

TODO:
-make it a real daemon, not just a user prog that sits open on the terminal
-start connecting more devices. I still wanna control the makeup air damper from all fart fans in the house
-add graphs to notifications - DONE!
