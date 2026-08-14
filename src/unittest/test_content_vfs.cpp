// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 VOPI Team

#include "test.h"

#include <cstring>
#include <fstream>
#include <sstream>

#include "content_vfs.h"
#include "filesys.h"
#include "log.h"
#include "porting.h"

// Builds minimal KPK v1 containers in-process (tools/kpk/FORMAT.md) and
// exercises the ContentVFS overlay through the public fs:: API — the same
// route the engine takes for builtin, game discovery and media.
class TestContentVFS : public TestBase
{
public:
	TestContentVFS() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestContentVFS"; }

	void runTests(IGameDef *gamedef);

	void testRejectsGarbage();
	void testMountAndStat();
	void testReadThroughFs();
	void testDirListingUnion();
	void testRealFileWins();

private:
	std::string makePack(const std::string &mount_spec);
	std::string m_pack_path;
	std::string m_mount_prefix; // resolved absolute prefix
};

static TestContentVFS g_test_instance;

static void putLEU32(std::string &s, u32 v)
{
	for (int i = 0; i < 4; i++)
		s += (char)((v >> (8 * i)) & 0xff);
}

static void putLEU64(std::string &s, u64 v)
{
	for (int i = 0; i < 8; i++)
		s += (char)((v >> (8 * i)) & 0xff);
}

// Three entries, stored raw, uncompressed index (INDEX_ZSTD off), unencrypted.
std::string TestContentVFS::makePack(const std::string &mount_spec)
{
	const std::string a_content = "hello from pack";
	const std::string b_content = std::string("\x01\x02\x03", 3);

	const std::string meta = "{\"id\":\"vfstest\",\"mount\":\"" + mount_spec +
			"\",\"type\":\"base\",\"version\":1}";

	const u64 meta_offset = 80;
	const u64 blob_start = meta_offset + meta.size();

	// entries in index (sorted by path): a.txt, e.txt (empty), sub/b.bin
	std::ostringstream index_ss;
	index_ss << "{\"files\":["
		<< "{\"p\":\"a.txt\",\"o\":" << blob_start
		<< ",\"s\":" << a_content.size() << ",\"r\":" << a_content.size()
		<< ",\"h\":\"0000000000000000000000000000000000000000\",\"n\":\""
		<< std::string(32, '0') << "\",\"f\":0},"
		<< "{\"p\":\"e.txt\",\"o\":" << (blob_start + a_content.size())
		<< ",\"s\":0,\"r\":0"
		<< ",\"h\":\"0000000000000000000000000000000000000000\",\"n\":\""
		<< std::string(32, '0') << "\",\"f\":0},"
		<< "{\"p\":\"sub/b.bin\",\"o\":" << (blob_start + a_content.size())
		<< ",\"s\":" << b_content.size() << ",\"r\":" << b_content.size()
		<< ",\"h\":\"0000000000000000000000000000000000000000\",\"n\":\""
		<< std::string(32, '0') << "\",\"f\":0}"
		<< "]}";
	const std::string index = index_ss.str();
	const u64 index_offset = blob_start + a_content.size() + b_content.size();

	std::string blob;
	blob += "KPK1";
	putLEU32(blob, 1);  // format version
	putLEU32(blob, 0);  // flags: unencrypted, index not compressed
	putLEU32(blob, 0);  // key version
	putLEU64(blob, meta_offset);
	putLEU64(blob, meta.size());
	putLEU64(blob, 0);  // preview offset
	putLEU64(blob, 0);  // preview size
	putLEU64(blob, index_offset);
	putLEU64(blob, index.size());
	blob += std::string(16, '\0'); // index nonce
	blob += meta;
	blob += a_content;
	blob += b_content;
	blob += index;

	const std::string path = getTestTempDirectory() + DIR_DELIM + "vfstest.kpk";
	std::ofstream os(path, std::ios::binary);
	os << blob;
	os.close();
	return path;
}

void TestContentVFS::runTests(IGameDef *gamedef)
{
	// Unique virtual prefix under path_user; nothing real exists there.
	const std::string mount_spec = "user:/__vfs_selftest__";
	m_mount_prefix = porting::path_user + DIR_DELIM + "__vfs_selftest__";
	m_pack_path = makePack(mount_spec);

	TEST(testRejectsGarbage);
	TEST(testMountAndStat);
	TEST(testReadThroughFs);
	TEST(testDirListingUnion);
	TEST(testRealFileWins);

	ContentVFS::get().unmountAll();
	fs::RecursiveDelete(m_mount_prefix);
}

void TestContentVFS::testRejectsGarbage()
{
	const std::string bad_path = getTestTempDirectory() + DIR_DELIM + "bad.kpk";
	std::ofstream os(bad_path, std::ios::binary);
	os << "definitely not a kpk file";
	os.close();

	std::string err;
	UASSERT(!ContentVFS::get().mountPackFile(bad_path, err));
	UASSERT(!err.empty());

	// Crafted header whose meta block bounds wrap u64: offset + size
	// overflows past file_size and must still be rejected.
	std::string evil;
	evil += "KPK1";
	putLEU32(evil, 1); // format version
	putLEU32(evil, 0); // flags
	putLEU32(evil, 0); // key version
	putLEU64(evil, 0xFFFFFFFFFFFFFFF0ULL); // meta_offset (wraps with size)
	putLEU64(evil, 0x20);                  // meta_size
	putLEU64(evil, 0); // preview offset
	putLEU64(evil, 0); // preview size
	putLEU64(evil, 80); // index offset
	putLEU64(evil, 8);  // index size
	evil += std::string(16, '\0');
	evil += std::string(64, 'x'); // trailing bytes so blocks "fit"

	const std::string evil_path = getTestTempDirectory() + DIR_DELIM + "evil.kpk";
	std::ofstream eos(evil_path, std::ios::binary);
	eos << evil;
	eos.close();

	UASSERT(!ContentVFS::get().mountPackFile(evil_path, err));
}

void TestContentVFS::testMountAndStat()
{
	std::string err;
	bool mounted = ContentVFS::get().mountPackFile(m_pack_path, err);
	if (!mounted)
		errorstream << "mountPackFile: " << err << std::endl;
	UASSERT(mounted);
	UASSERT(ContentVFS::get().isActive());
	UASSERT(ContentVFS::get().getPack("vfstest") != nullptr);

	// duplicate id is refused
	UASSERT(!ContentVFS::get().mountPackFile(m_pack_path, err));

	UASSERT(ContentVFS::get().statPath(m_mount_prefix) == ContentVFS::Stat::Dir);
	UASSERT(ContentVFS::get().statPath(m_mount_prefix + DIR_DELIM + "a.txt")
			== ContentVFS::Stat::File);
	UASSERT(ContentVFS::get().statPath(m_mount_prefix + DIR_DELIM + "sub")
			== ContentVFS::Stat::Dir);
	UASSERT(ContentVFS::get().statPath(m_mount_prefix + DIR_DELIM + "nope")
			== ContentVFS::Stat::NotFound);
}

void TestContentVFS::testReadThroughFs()
{
	// The whole point of the overlay: plain fs:: calls see pack content.
	UASSERT(fs::PathExists(m_mount_prefix + DIR_DELIM + "a.txt"));
	UASSERT(fs::IsFile(m_mount_prefix + DIR_DELIM + "a.txt"));
	UASSERT(fs::IsDir(m_mount_prefix + DIR_DELIM + "sub"));
	UASSERT(!fs::IsFile(m_mount_prefix + DIR_DELIM + "sub"));

	std::string content;
	UASSERT(fs::ReadFile(m_mount_prefix + DIR_DELIM + "a.txt", content, false));
	UASSERTEQ(std::string, content, "hello from pack");

	UASSERT(fs::ReadFile(m_mount_prefix + DIR_DELIM + "sub" + DIR_DELIM + "b.bin",
			content, false));
	UASSERTEQ(size_t, content.size(), 3);

	UASSERT(fs::ReadFile(m_mount_prefix + DIR_DELIM + "e.txt", content, false));
	UASSERT(content.empty());

	// AbsolutePath falls back to the lexical form for covered paths
	const std::string abs = fs::AbsolutePath(
			m_mount_prefix + DIR_DELIM + "sub" + DIR_DELIM + ".." + DIR_DELIM + "a.txt");
	UASSERT(!abs.empty());
	UASSERT(abs.find("..") == std::string::npos);
}

void TestContentVFS::testDirListingUnion()
{
	// Mount root listing comes purely from the pack
	std::vector<fs::DirListNode> listing = fs::GetDirListing(m_mount_prefix);
	bool saw_a = false, saw_sub = false, saw_e = false;
	for (const auto &n : listing) {
		if (n.name == "a.txt") { saw_a = true; UASSERT(!n.dir); }
		if (n.name == "sub")   { saw_sub = true; UASSERT(n.dir); }
		if (n.name == "e.txt") { saw_e = true; UASSERT(!n.dir); }
	}
	UASSERT(saw_a && saw_sub && saw_e);

	// The mount point surfaces as a directory when listing its real parent
	bool saw_mount = false;
	for (const auto &n : fs::GetDirListing(porting::path_user)) {
		if (n.name == "__vfs_selftest__") {
			saw_mount = true;
			UASSERT(n.dir);
		}
	}
	UASSERT(saw_mount);
}

void TestContentVFS::testRealFileWins()
{
	// A loose file at the same path must shadow the pack entry (dev overlay)
	const std::string dir = m_mount_prefix;
	const std::string real_file = dir + DIR_DELIM + "a.txt";
	UASSERT(fs::CreateAllDirs(dir));
	std::ofstream os(real_file, std::ios::binary);
	os << "real file wins";
	os.close();

	std::string content;
	UASSERT(fs::ReadFile(real_file, content, false));
	UASSERTEQ(std::string, content, "real file wins");

	fs::DeleteSingleFileOrEmptyDirectory(real_file);
	UASSERT(fs::ReadFile(real_file, content, false));
	UASSERTEQ(std::string, content, "hello from pack");
}
