import pdfplumber

SRC = r"C:\Users\a\AppData\Local\Temp\codebuddy-dropped-files\a4e16a25-86a2-4949-abcb-1fe02e001165\电机CAN协议说明 V2.35.pdf"
OUT = r"d:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchPublish\tmp\mwd_can_v235.txt"

with pdfplumber.open(SRC) as pdf:
    total = len(pdf.pages)
    with open(OUT, "w", encoding="utf-8") as handle:
        for index, page in enumerate(pdf.pages):
            text = page.extract_text() or ""
            handle.write("\n===== PAGE %d =====\n" % (index + 1))
            handle.write(text)
print("pages=%d out=%s" % (total, OUT))
