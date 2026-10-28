Soup Engine for EOS M
========================

Film JPEGs from the CR2's embedded Canon preview. Original EOS M, firmware
2.0.2, Magic Lantern module API 8. Experimental port with limited user testing:
film JPEG generation works after the I/O fix, at about 45 seconds per photo.
Output size for that timing was not confirmed. Broad stability is unverified.
The new 4:2:0 / buffered-I/O / quarter-size update still needs camera testing.

Use and output
--------------

Enable soup.mo, restart, then Shoot > Soup Engine. Select a stock and turn
Soup Engine on. Existing and new CR2 files in DCIM are processed. Originals
are kept. Results: IMG_1234.FLM.JPG beside IMG_1234.CR2. Existing results
are skipped. Stock files live in ML/FILM; restart after changing them.
Output size offers Half, Full and Quarter preview. Quarter is the fastest;
on an EOS M full-size preview it produces 1296x864. Existing size settings keep
their meanings. CR2s stay read-only; USB debugging is disabled.

Camera settings
---------------

Use RAW or RAW+JPEG, a colour Picture Style and sRGB. No live view effect,
RAW sensor development or MLV video grading. Camera playback of the new JPEG
is not guaranteed. Turn the feature off to cancel. For initial hardware
testing, disable Canon auto power off and let a develop finish before shutdown.

:Summary: Film JPEGs from CR2 previews on the original EOS M.
:Author: EOS M port generated with OpenAI Codex; engine by jiijii0
:License: GPL v2
:Website: https://github.com/jiijii0/soup-engine
