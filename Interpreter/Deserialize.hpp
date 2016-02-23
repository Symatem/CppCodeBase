#include "Serialize.hpp"

#define getSymbolByName(Name) \
    Symbol Name##Symbol = task.context.getGuaranteed(task.block, PreDef_##Name);

#define getSymbolObjectByName(Name) \
    getSymbolByName(Name) \
    auto Name##SymbolObject = task.context.getSymbolObject(Name##Symbol);

#define checkBlobType(Name, expectedType) \
if(task.context.query(1, {Name##Symbol, PreDef_BlobType, expectedType}) == 0) \
    throw Exception("Invalid Blob Type");

#define getUncertainSymbolObjectByName(Name, DefaultValue) \
    Symbol Name##Symbol; ArchitectureType Name##Value; \
    if(task.context.getUncertain(task.block, PreDef_##Name, Name##Symbol)) { \
        checkBlobType(Name, PreDef_Natural) \
        Name##Value = task.context.getSymbolObject(Name##Symbol)->accessBlobAt<ArchitectureType>(); \
    } else \
        Name##Value = DefaultValue;

class Deserialize {
    Task& task;
    const char *pos, *end, *tokenBegin;
    ArchitectureType row, column;
    Symbol package;

    void throwException(const char* message) {
        throw Exception(message, {
            {PreDef_Row, task.context.createFromData(row)},
            {PreDef_Column, task.context.createFromData(column)}
        });
    }

    Vector<Symbol> stack;
    Symbol parentEntry, currentEntry;
    std::map<SymbolObject*, Symbol, Context::BlobIndexCompare> locals;

    // TODO: Refactor to be a blob based vector
    Symbol popQueue() {
        Symbol oldElement = task.context.getGuaranteed(currentEntry, PreDef_Queue),
               symbol = task.context.getGuaranteed(oldElement, PreDef_Value),
               queueBegin = oldElement;
        if(task.context.getUncertain(queueBegin, PreDef_Next, queueBegin))
            task.context.setSolitary({currentEntry, PreDef_Queue, queueBegin});
        else
            task.context.unlink(currentEntry, PreDef_Queue);
        task.context.destroy(oldElement);
        return symbol;
    }

    void nextSymbol(Symbol stackEntry, Symbol symbol) {
        if(task.context.valueSetCountIs(stackEntry, PreDef_UnnestEntity, 0)) {
            Symbol queueEnd, newElement = task.context.create({{PreDef_Value, symbol}});
            if(task.context.getUncertain(stackEntry, PreDef_Queue, queueEnd)) {
                while(task.context.getUncertain(queueEnd, PreDef_Next, queueEnd));
                task.context.link({queueEnd, PreDef_Next, newElement});
            } else
                task.context.link({stackEntry, PreDef_Queue, newElement});
        } else {
            Symbol entity = task.context.getGuaranteed(stackEntry, PreDef_UnnestEntity),
                   attribute = task.context.getGuaranteed(stackEntry, PreDef_UnnestAttribute);
            task.context.link({entity, attribute, symbol});
            task.context.unlink(stackEntry, PreDef_UnnestEntity);
        }
    }

    void parseToken(bool isText = false) {
        if(pos > tokenBegin) {
            Symbol symbol;
            if(isText)
                symbol = task.context.createFromData(tokenBegin, pos-tokenBegin);
            else if(*tokenBegin == '#') {
                symbol = task.context.createFromData(tokenBegin, pos-tokenBegin);
                SymbolObject* symbolObject = task.context.getSymbolObject(symbol);
                auto iter = locals.find(symbolObject);
                if(iter == locals.end())
                    locals.insert({symbolObject, symbol});
                else {
                    task.context.destroy(symbol);
                    symbol = (*iter).second;
                }
            } else if(pos-tokenBegin > strlen(HRLRawBegin) && memcmp(tokenBegin, HRLRawBegin, 4) == 0) {
                const char* src = tokenBegin+strlen(HRLRawBegin);
                ArchitectureType nibbleCount = pos-src;
                if(nibbleCount == 0)
                    throwException("Empty raw data");
                symbol = task.context.create();
                SymbolObject* symbolObject = task.context.getSymbolObject(symbol);
                symbolObject->allocateBlob(nibbleCount*4);
                char* dst = reinterpret_cast<char*>(symbolObject->blobData.get()), odd = 0, nibble;
                while(src < pos) {
                    if(*src >= '0' && *src <= '9')
                        nibble = *src-'0';
                    else if(*src >= 'A' && *src <= 'F')
                        nibble = *src-'A'+0xA;
                    else
                        throwException("Non hex characters");
                    if(odd == 0) {
                        *dst = nibble;
                        odd = 1;
                    } else {
                        *(dst++) |= nibble<<4;
                        odd = 0;
                    }
                    ++src;
                }
            } else {
                ArchitectureType mantissa = 0, devisor = 0;
                bool isNumber = true, negative = (*tokenBegin == '-');
                const char* src = tokenBegin+negative;
                // TODO What if too long, precision loss?
                while(src < pos) {
                    devisor *= 10;
                    if(*src >= '0' && *src <= '9') {
                        mantissa *= 10;
                        mantissa += *src-'0';
                    } else if(*src == '.') {
                        if(devisor > 0) {
                            isNumber = false;
                            break;
                        }
                        devisor = 1;
                    } else {
                        isNumber = false;
                        break;
                    }
                    ++src;
                }
                if(isNumber && devisor != 1) {
                    if(devisor > 0) {
                        double value = mantissa;
                        value /= devisor;
                        if(negative) value *= -1;
                        symbol = task.context.createFromData(value);
                    } else if(negative)
                        symbol = task.context.createFromData(-static_cast<int64_t>(mantissa));
                    else
                        symbol = task.context.createFromData(mantissa);
                } else
                    symbol = task.context.createFromData(tokenBegin, pos-tokenBegin);
                symbol = task.indexBlob(symbol);
            }
            task.context.link({package, PreDef_Holds, symbol}, false);
            nextSymbol(currentEntry, symbol);
        }
        tokenBegin = pos+1;
    }

    void fillInAnonymous(Symbol& entity) {
        if(entity != PreDef_Void)
            return;
        entity = task.context.create();
        task.context.link({currentEntry, PreDef_Entity, entity});
        task.context.link({package, PreDef_Holds, entity});
        nextSymbol(parentEntry, entity);
    }

    void seperateTokens(bool semicolon) {
        parseToken();

        Symbol entity, queue;
        if(!task.context.getUncertain(currentEntry, PreDef_Entity, entity))
            entity = PreDef_Void;
        if(!task.context.getUncertain(currentEntry, PreDef_Queue, queue))
            queue = PreDef_Void;

        if(queue == PreDef_Void) {
            if(semicolon) {
                if(entity != PreDef_Void)
                    throwException("Pointless semicolon");
                fillInAnonymous(entity);
            }
            return;
        }

        if(semicolon && queue != PreDef_Void && task.context.valueSetCountIs(queue, PreDef_Next, 0)) {
            if(entity == PreDef_Void) {
                entity = popQueue();
                task.context.link({currentEntry, PreDef_Entity, entity});
                nextSymbol(parentEntry, entity);
            } else
                task.context.link({entity, popQueue(), entity});
            return;
        }

        fillInAnonymous(entity);
        if(semicolon)
            task.context.unlink(parentEntry, PreDef_UnnestEntity);
        else
            task.context.setSolitary({parentEntry, PreDef_UnnestEntity, entity});
        Symbol attribute = popQueue();
        task.context.setSolitary({parentEntry, PreDef_UnnestAttribute, attribute});

        while(!task.context.valueSetCountIs(currentEntry, PreDef_Queue, 0))
            task.context.link({entity, attribute, popQueue()});
    }

    public:
    Deserialize(Task& _task) :task(_task), stack(_task.context) {
        package = task.context.getGuaranteed(task.block, PreDef_Package);
        getSymbolObjectByName(Input)
        checkBlobType(Input, PreDef_Text)

        currentEntry = task.context.create();
        task.context.link({task.block, PreDef_Holds, currentEntry});
        stack.push_back(currentEntry);

        row = column = 1;
        tokenBegin = pos = reinterpret_cast<const char*>(InputSymbolObject->blobData.get());
        end = pos+InputSymbolObject->blobSize/8;
        while(pos < end) {
            switch(*pos) {
                case '\n':
                    parseToken();
                    column = 0;
                    ++row;
                    break;
                case '\t':
                    column += 3;
                case ' ':
                    parseToken();
                    break;
                case '"':
                    tokenBegin = pos+1;
                    while(true) {
                        if(pos == end)
                            throwException("Unterminated text");
                        bool prev = (*pos != '\\');
                        ++pos;
                        if(prev) {
                            if(*pos == '\\')
                                continue;
                            if(*pos == '"')
                                break;
                        }
                    }
                    parseToken(true);
                    break;
                case '(':
                    parseToken();
                    parentEntry = currentEntry;
                    currentEntry = task.context.create();
                    task.context.link({task.block, PreDef_Holds, currentEntry});
                    stack.push_back(currentEntry);
                    break;
                case ';':
                    if(stack.size() == 1)
                        throwException("Semicolon outside of any brackets");
                    seperateTokens(true);
                    if(!task.context.valueSetCountIs(currentEntry, PreDef_UnnestEntity, 0))
                        throwException("Unnesting failed");
                    break;
                case ')': {
                    if(stack.size() == 1)
                        throwException("Unmatched closing bracket");
                    seperateTokens(false);
                    if(stack.size() == 2 && task.context.valueSetCountIs(parentEntry, PreDef_UnnestEntity, 0)) {
                        locals.clear();
                        auto topIter = task.context.topIndex.find(task.context.getGuaranteed(currentEntry, PreDef_Entity));
                        if(topIter != task.context.topIndex.end() && topIter->second->subIndices[EAV].empty())
                            throwException("Nothing declared");
                    }
                    if(!task.context.valueSetCountIs(currentEntry, PreDef_UnnestEntity, 0))
                        throwException("Unnesting failed");
                    task.context.destroy(currentEntry);
                    stack.pop_back();
                    currentEntry = parentEntry;
                    parentEntry = (stack.size() < 2) ? PreDef_Void : stack[stack.size()-2];
                }   break;
            }
            ++column;
            ++pos;
        }
        parseToken();

        if(stack.size() != 1)
            throwException("Missing closing bracket");
        if(!task.context.valueSetCountIs(currentEntry, PreDef_UnnestEntity, 0))
            throwException("Unnesting failed");
        if(task.context.valueSetCountIs(currentEntry, PreDef_Queue, 0))
            throwException("Empty Input");

        Symbol OutputSymbol;
        if(task.context.getUncertain(task.block, PreDef_Output, OutputSymbol)) {
            Symbol TargetSymbol = task.getTargetSymbol();
            task.context.unlink(TargetSymbol, OutputSymbol);
            while(!task.context.valueSetCountIs(currentEntry, PreDef_Queue, 0))
                task.context.link({TargetSymbol, OutputSymbol, popQueue()});
        }
        task.popCallStack();
    }
};
