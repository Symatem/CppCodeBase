#include "POSIX.hpp"

Thread thread;

Symbol createFromFile(const char* path) {
    Integer32 fd = open(path, O_RDONLY);
    if(fd < 0)
        return Ontology::VoidSymbol;
    Symbol dst = Storage::createSymbol();
    Ontology::link({dst, Ontology::BlobTypeSymbol, Ontology::TextSymbol});
    NativeNaturalType length = lseek(fd, 0, SEEK_END);
    Storage::increaseBlobSize(dst, 0, length*8);
    lseek(fd, 0, SEEK_SET);
    char buffer[64];
    NativeNaturalType dstIndex = 0;
    while(length > 0) {
        NativeNaturalType count = min((NativeNaturalType)sizeof(buffer), length);
        read(fd, buffer, count);
        for(NativeNaturalType j = 0; j < count; ++j)
            Storage::writeBlobAt<char>(dst, dstIndex++, buffer[j]);
        length -= count;
    }
    close(fd);
    return dst;
}

void loadFromPath(Symbol parentPackage, bool execute, char* path) {
    NativeNaturalType pathLen = strlen(path);
    if(path[pathLen-1] == '/')
        path[pathLen-1] = 0;
    struct stat s;
    char buffer[64];
    if(stat(path, &s) != 0)
        return;
    if(s.st_mode & S_IFDIR) {
        DIR* dp = opendir(path);
        assert(dp);
        NativeNaturalType slashIndex = 0;
        for(NativeNaturalType i = pathLen-1; i > 0; --i)
            if(path[i] == '/') {
                slashIndex = i+1;
                break;
            }
        Storage::bitwiseCopy(reinterpret_cast<NativeNaturalType*>(buffer),
                             reinterpret_cast<NativeNaturalType*>(path),
                             0, slashIndex*8, (pathLen-slashIndex)*8);
        buffer[pathLen-slashIndex] = 0;
        Symbol package = Ontology::createFromString(const_cast<const char*>(buffer));
        Ontology::blobIndex.insertElement(package);
        if(parentPackage == Ontology::VoidSymbol)
            parentPackage = package;
        thread.link({package, Ontology::HoldsSymbol, parentPackage});
        struct dirent* entry;
        while((entry = readdir(dp)))
            if(entry->d_name[0] != '.') {
                sprintf(buffer, "%s/%s", path, entry->d_name);
                loadFromPath(package, execute, buffer);
            }
        closedir(dp);
    } else if(s.st_mode & S_IFREG) {
        if(!Storage::substrEqual<true>(path, ".sym"))
            return;
        Symbol file = createFromFile(path);
        assert(file != Ontology::VoidSymbol);
        thread.deserializationTask(file, parentPackage);
        if(thread.uncaughtException()) {
            printf("Exception occurred while deserializing file %s.\n", path);
            exit(2);
        }
        if(!execute)
            return;
        if(!thread.executeDeserialized()) {
            printf("Nothing to execute in file %s.\n", path);
            exit(3);
        } else if(thread.uncaughtException()) {
            printf("Exception occurred while executing file %s.\n", path);
            exit(4);
        }
    }
}

Integer32 main(Integer32 argc, Integer8** argv) {
    loadStorage("./data");

    bool execute = false;
    for(NativeNaturalType i = 1; i < argc; ++i) {
        if(Storage::substrEqual(argv[i], "-h")) {
            printf("This is not the help page you are looking for.\n");
            printf("No, seriously, RTFM.\n");
            exit(4);
        } else if(Storage::substrEqual(argv[i], "-e")) {
            execute = true;
            continue;
        }
        loadFromPath(Ontology::VoidSymbol, execute, argv[i]);
    }
    thread.clear();
    printStats();

    unloadStorage();
    return 0;
}
