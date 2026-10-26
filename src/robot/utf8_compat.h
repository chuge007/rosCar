#pragma once

// String data is ASCII source text and is decoded as UTF-8 at runtime.
#define CRAWLING_TEXT(str) QString::fromUtf8(str)
