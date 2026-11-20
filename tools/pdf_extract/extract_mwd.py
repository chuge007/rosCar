import pdfplumber

SRC = r"C:\Users\a\AppData\Local\Temp\codebuddy-dropped-files\a81d2b5f-d88d-4405-bfd4-9037f48fff84\伺服电机控制协议V3.8.pdf"
OUT = r"d:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchPublish\tmp\mwd_v38_full.txt"

with pdfplumber.open(SRC) as pdf:
    total = len(pdf.pages)
    with open(OUT, "w", encoding="utf-8") as handle:
        for index, page in enumerate(pdf.pages):
            text = page.extract_text() or ""
            handle.write("\n===== PAGE %d =====\n" % (index + 1))
            handle.write(text)
print("pages=%d out=%s" % (total, OUT))
