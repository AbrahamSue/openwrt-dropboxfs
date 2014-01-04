
%option reentrance case-insensitive

%%
^HTTP/1.[01]\ 206.*\r$		{ }
^HTTP/1.[01]\ 302.*\r$		{ }
^HTTP/1.[01]\ [0-9]+.*\r$	{ }

^Accept-Ranges:\ bytes		{ }
^Content-disposition: attachment; filename=".*"\r$
^Content-range: bytes [0-9]+-[0-9]+/[0-9]+	{ }
^Content-Type: application/octet-stream		{ }
^Content-Length: [0-9]+\r$			{ }
^Connection: keep-alive\r$			{ }
^\r$				{ }
