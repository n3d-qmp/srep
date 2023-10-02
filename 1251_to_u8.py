import os

def try_to_dec(d,e):
	try:
		return d.decode(e)
	except:
		return None

def decode_any(d):
	cp1251 = try_to_dec(d, "cp1251")
	utf_8 = try_to_dec(d, "utf-8")
	if utf_8 is None and cp1251 is not None:
		return cp1251
	return utf_8

def cnv(d):
	for (path,dirs,files) in os.walk(d):
		if "/.git" in path:
			continue
		for file in files:
			file = os.path.join(path,file)
			with open(file, "rb") as f:
				d = f.read()
			nd = decode_any(d)
			if nd is None:
				continue
			nd =nd.encode("utf-8")
			if d!=nd:
				with open(file, "wb") as f:
					f.write(nd)
					f.close()
				print(f"Processed: {file}")


if __name__ == '__main__':
	cnv(".")
