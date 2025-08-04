import io
import os
import shutil
import zipfile

import requests

os.makedirs("nasm", exist_ok=True)

url = "https://www.nasm.us/pub/nasm/releasebuilds/2.16.01/win64/nasm-2.16.01-win64.zip"
response = requests.get(url)
with zipfile.ZipFile(io.BytesIO(response.content)) as archive:
    for path in archive.namelist():
        if path.endswith("nasm.exe"):
            with open("nasm/nasm.exe", "wb") as file:
                file.write(archive.open(path).read())
