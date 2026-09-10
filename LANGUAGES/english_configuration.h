#ifndef ENGLISH_CONFIG_H
#define ENGLISH_CONFIG_H

#include "../LEXICON/lexicon_fsa.h"
#include "../LEXICON/lexicon_loader.h"
#include "../RULES/plural_rule.h"
#include "../RULES/verb_rule.h"
#include "../RULES/adj_rule.h"
#include "../RULES/derivation_rule.h"
#include "../ANALYSIS/analyzer.h"
#include <memory>
#include <vector>

/**
 * EnglishConfig: Configuration for English morphology
 *
 * Responsibilities:
 *   - Load English lexicon (stems with POS and stem class)
 *   - Configure morphological rules (plural formation, etc.)
 *
 * Design Principle:
 *   The analyzer core is language-independent.
 *   This file contains all English-specific linguistic knowledge.
 */
class EnglishConfig {
public:
    static void load_lexicon(LexiconFSA& lexicon) {

        // ══════════════════════════════════════════════════════════════
        // NOUNS, REG  (+s plural)
        // ══════════════════════════════════════════════════════════════

        // Animals
        for (auto s : {"animal","ant","ape","bat","bear","bee","bird","boar",
                       "bug","bull","calf","cat","cow","crab","crane","crow",
                       "deer","dog","dove","duck","eagle","eel","elephant",
                       "fawn","frog","giraffe","goat","hawk","hen","horse",
                       "hound","jay","lamb","leopard","lion","lizard","mole",
                       "moth","mule","owl","parrot","penguin","pig","pigeon",
                       "rabbit","ram","rat","raven","salmon","seal","shark",
                       "snail","snake","sparrow","spider","swan","tiger",
                       "toad","trout","turtle","wasp","weasel","wolf","worm",
                       "yak","zebra"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // People / social
        for (auto s : {"adult","aunt","baron","boy","brother","captain",
                       "child","citizen","clerk","colonel","cook","cousin",
                       "criminal","customer","doctor","driver","editor",
                       "employer","engineer","expert","farmer","father",
                       "friend","general","girl","god","governor","guard",
                       "guest","heir","hero","husband","immigrant","infant",
                       "instructor","journalist","judge","kid","king","knight",
                       "lawyer","leader","lord","maiden","man","manager",
                       "merchant","minister","monk","neighbor","nephew",
                       "nurse","officer","parent","peasant","person","pilot",
                       "poet","politician","president","priest","prince",
                       "prisoner","professor","prophet","queen","rebel",
                       "saint","sailor","scholar","senator","servant",
                       "sister","slave","soldier","son","student","surgeon",
                       "teacher","tenant","tourist","trader","trainer",
                       "villain","warrior","wife","worker","writer"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // Body parts
        for (auto s : {"arm","back","beard","bone","brain","breast","calf",
                       "cheek","chest","chin","ear","elbow","eye","face",
                       "finger","forehead","hair","hand","head","heart",
                       "heel","hip","jaw","knee","knuckle","leg","lip",
                       "lung","nail","neck","nerve","nose","rib","shin",
                       "shoulder","skin","skull","spine","stomach","temple",
                       "thigh","throat","thumb","toe","tongue","vein",
                       "wrist"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // Food and drink
        for (auto s : {"apple","apricot","avocado","bacon","banana","bean",
                       "beer","beet","bread","broccoli","burger","butter",
                       "cake","carrot","cheese","cherry","chicken","chip",
                       "chocolate","coconut","cookie","corn","cream","crop",
                       "cucumber","egg","fig","flour","fruit","garlic",
                       "grain","grape","ham","herb","lemon","lime","mango",
                       "meal","meat","melon","milk","mushroom","mustard",
                       "noodle","nut","oat","oil","onion","orange","peach",
                       "pear","pepper","pie","plum","potato","pumpkin",
                       "raisin","rice","roll","salad","salt","sauce","seed",
                       "soup","steak","sugar","tart","tea","tomato",
                       "vegetable","walnut","watermelon","wheat","wine",
                       "yogurt"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // Nature / geography
        for (auto s : {"air","bay","beach","boulder","brook","canyon","cave",
                       "cliff","cloud","coast","creek","desert","dune",
                       "earthquake","field","fire","fjord","flood","flower",
                       "fog","forest","fountain","garden","glacier","ground",
                       "gulf","harbor","hill","horizon","hurricane","island",
                       "jungle","lake","land","leaf","light","lightning",
                       "meadow","moon","mountain","mud","ocean","path",
                       "peak","petal","plain","planet","plant","pond",
                       "pool","rain","rainbow","reef","river","rock","root",
                       "sand","sea","shore","sky","snow","soil","spring",
                       "star","stem","stone","storm","stream","sun","swamp",
                       "tide","tree","trunk","valley","vine","volcano",
                       "wave","wind","wood"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // Built environment / objects
        for (auto s : {"airport","apartment","arch","arena","attic","barn",
                       "barrel","basement","basket","bath","bed","belt",
                       "bench","boat","bomb","book","boot","border","bottle",
                       "bowl","box","brick","bridge","broom","bucket",
                       "building","bullet","cabinet","candle","car","card",
                       "carpet","cart","ceiling","cellar","chain","chair",
                       "channel","chart","clock","closet","coat","coin",
                       "column","computer","cord","corner","crown","cup",
                       "curtain","cushion","dam","dart","desk","device",
                       "door","drawer","drum","envelope","exit","fence",
                       "film","flag","floor","fork","frame","gate","gem",
                       "gift","glass","glove","gun","hammer","handle",
                       "hat","helmet","hook","hospital","hotel","jar",
                       "jewel","key","knife","label","lamp","lantern",
                       "lid","lock","log","map","mirror","monument","mug",
                       "nail","needle","net","pad","page","paint","pan",
                       "pen","pencil","pillar","pin","pipe","pistol","pit",
                       "plate","pocket","pole","pot","prize","rail","ribbon",
                       "ring","road","rocket","roof","room","rope","rug",
                       "ruler","sack","sail","screw","seat","shelf","shield",
                       "ship","shirt","shoe","sign","sock","spear","spoon",
                       "stadium","stair","stamp","street","suit","sword",
                       "table","tank","tent","ticket","tile","tool","tower",
                       "town","trap","truck","tunnel","umbrella","uniform",
                       "vase","wall","weapon","wheel","wire","yard"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // Abstract / concepts
        for (auto s : {"act","age","aim","amount","art","award","balance",
                       "belief","benefit","bond","burden","cause","chance",
                       "change","claim","concept","condition","contrast",
                       "cost","count","deal","death","debt","demand",
                       "detail","dream","effect","effort","element","end",
                       "error","event","example","experience","experiment",
                       "fact","fault","fear","feeling","form","goal","grant",
                       "group","growth","habit","harm","help","hint","honor",
                       "hope","impact","increase","interest","issue","job",
                       "kind","lack","law","level","limit","link","loan",
                       "loss","mark","meaning","method","mind","moment",
                       "movement","need","network","note","number","object",
                       "order","outcome","output","pain","part","pattern",
                       "peace","plan","point","power","problem","profit",
                       "program","project","proof","question","rate","reason",
                       "record","result","reward","right","risk","role","rule",
                       "sign","skill","source","standard","statement","step",
                       "strength","subject","sum","support","task","term",
                       "thought","threat","title","trend","turn","view",
                       "value","war","weight","word","work","year","zone"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // Time
        for (auto s : {"dawn","day","decade","delay","dusk","era","hour",
                       "instant","interval","minute","month","morning",
                       "night","noon","period","season","second","sunset",
                       "week","weekend"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::REG));
        }

        // ══════════════════════════════════════════════════════════════
        // NOUNS, Y_FINAL  (y -> ies plural)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"ability","academy","agony","allegory","allergy",
                       "ambiguity","analogy","anatomy","anomaly","anxiety",
                       "apology","army","assembly","authority","autonomy",
                       "baby","bakery","battery","beauty","biology",
                       "boundary","brewery","bribery","brutality","bureaucracy",
                       "butterfly","canopy","capacity","category","cavity","city",
                       "celebrity","ceremony","charity","chivalry","colony",
                       "comedy","community","complexity","conspiracy","country",
                       "county","currency","custody","democracy","dependency",
                       "destiny","diplomacy","discovery","diversity","duty",
                       "dynasty","eccentricity","economy","embassy","emergency",
                       "energy","entity","entry","eulogy","fantasy","felony",
                       "fertility","flexibility","fluency","frequency","galaxy",
                       "gallery","glory","gravity","harmony","honesty","humility",
                       "identity","ideology","immunity","inequality","infancy",
                       "injury","inquiry","integrity","intensity","irony",
                       "jealousy","journey","jury","legacy","liberty","library",
                       "literacy","lottery","loyalty","luxury","majority",
                       "melody","memory","mercury","ministry","monopoly",
                       "monarchy","morality","mystery","nationality","navy",
                       "obesity","opportunity","party","penalty","philosophy",
                       "photography","policy","priority","privacy","prophecy",
                       "quality","quantity","rally","reality","remedy","rivalry",
                       "safety","salary","sanctuary","secrecy","serenity",
                       "slavery","society","sovereignty","strategy","study",
                       "subsidy","summary","sympathy","theory","therapy",
                       "tragedy","treasury","trophy","uncertainty","unity",
                       "urgency","utility","vacancy","variety","victory",
                       "vocabulary","vulnerability","warranty"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::Y_FINAL));
        }

        // ══════════════════════════════════════════════════════════════
        // NOUNS, SIBILANT  (+es plural: kisses, churches, foxes)
        // ══════════════════════════════════════════════════════════════

        // -ss / -s
        for (auto s : {"address","atlas","bonus","boss","bus","canvas","chorus",
                       "class","circus","compass","consensus","crisis",
                       "focus","gas","genius","grass","guess","kiss","lens",
                       "mass","mess","minus","moss","pass","plus","press",
                       "process","progress","radius","status","stress",
                       "success","surplus","toss","virus"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::SIBILANT));
        }

        // -ch
        for (auto s : {"arch","beach","bench","branch","church","coach",
                       "couch","ditch","hatch","inch","latch","lunch",
                       "match","patch","peach","perch","porch","punch",
                       "ranch","reach","research","scratch","speech","switch",
                       "touch","trench","watch","witch"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::SIBILANT));
        }

        // -sh
        for (auto s : {"brush","bush","clash","crash","crush","dash","dish",
                       "flash","flesh","flush","lash","marsh","mesh","rush",
                       "slash","splash","thrush","wish"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::SIBILANT));
        }

        // -x / -z
        for (auto s : {"ax","box","fox","fuzz","hex","hoax","jazz","mix",
                       "prefix","suffix","tax","wax"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::SIBILANT));
        }

        // ══════════════════════════════════════════════════════════════
        // NOUNS, IRREGULAR
        // ══════════════════════════════════════════════════════════════

        // IRR_MOUSE (vowel-change plurals)
        auto mouse = std::make_shared<Lexeme>("mouse", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
        mouse->add_irregular_form(Lexeme::Feature::PLURAL, "mice");
        lexicon.insert_lexeme(mouse);

        auto louse = std::make_shared<Lexeme>("louse", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
        louse->add_irregular_form(Lexeme::Feature::PLURAL, "lice");
        lexicon.insert_lexeme(louse);

        auto goose = std::make_shared<Lexeme>("goose", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
        goose->add_irregular_form(Lexeme::Feature::PLURAL, "geese");
        lexicon.insert_lexeme(goose);

        auto foot = std::make_shared<Lexeme>("foot", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
        foot->add_irregular_form(Lexeme::Feature::PLURAL, "feet");
        lexicon.insert_lexeme(foot);

        auto tooth = std::make_shared<Lexeme>("tooth", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
        tooth->add_irregular_form(Lexeme::Feature::PLURAL, "teeth");
        lexicon.insert_lexeme(tooth);

        auto man = std::make_shared<Lexeme>("man", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
        man->add_irregular_form(Lexeme::Feature::PLURAL, "men");
        lexicon.insert_lexeme(man);

        auto woman = std::make_shared<Lexeme>("woman", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
        woman->add_irregular_form(Lexeme::Feature::PLURAL, "women");
        lexicon.insert_lexeme(woman);

        // IRR_CHILD (-ren plurals)
        auto child = std::make_shared<Lexeme>("child", PartOfSpeech::NOUN, StemClass::IRR_CHILD);
        child->add_irregular_form(Lexeme::Feature::PLURAL, "children");
        lexicon.insert_lexeme(child);

        // IRR_OX (-en plurals)
        auto ox = std::make_shared<Lexeme>("ox", PartOfSpeech::NOUN, StemClass::IRR_OX);
        ox->add_irregular_form(Lexeme::Feature::PLURAL, "oxen");
        lexicon.insert_lexeme(ox);

        // IRR_SHEEP (zero plurals)
        for (auto s : {"aircraft","bison","cod","deer","elk","fish","moose",
                       "series","sheep","species","spacecraft","swine","trout"}) {
            auto lex = std::make_shared<Lexeme>(s, PartOfSpeech::NOUN, StemClass::IRR_SHEEP);
            lex->add_irregular_form(Lexeme::Feature::PLURAL, s);
            lexicon.insert_lexeme(lex);
        }

        // ══════════════════════════════════════════════════════════════
        // VERBS, REG  (direct concatenation: walk+s, walk+ed, walk+ing)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"accept","act","add","affect","answer","appear",
                       "assist","attach","attend","avoid","benefit","borrow",
                       "call","cancel","check","cheer","claim","clean","clear",
                       "climb","collect","comment","complain","confirm",
                       "connect","consider","contain","control","count","cover",
                       "crash","crawl","crossover","delay","deliver","demand",
                       "depend","design","develop","differ","discover","dream",
                       "earn","elect","end","enroll","exist","expand",
                       "explain","fail","fill","finish","follow","form",
                       "fund","gain","happen","harm","help","honor","hunt",
                       "impact","invent","join","jump","kill","last","laugh",
                       "learn","link","listen","look","lower","maintain",
                       "march","matter","mention","need","offer","open",
                       "order","own","paint","pass","perform","pick","plan",
                       "plant","play","point","present","print","pull",
                       "push","question","reach","react","read","record",
                       "reflect","remain","repair","repeat","resist","return",
                       "reveal","roll","run","seem","select","show","signal",
                       "sing","sleep","smell","sound","stand","start","stay",
                       "step","support","talk","tell","think","touch","train",
                       "travel","turn","understand","visit","wait","walk",
                       "want","watch","work","yield"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::VERB, StemClass::REG));
        }

        // ── Nouns that also function as verbs (REG) ──────────────────
        for (auto s : {"act","aim","award","camp","chair","chart","coat",
                       "corner","count","cover","dream","dust","end","exit",
                       "farm","guard","hand","harm","head","help","hint",
                       "honor","hunt","impact","interest","jail","mark",
                       "need","network","note","number","offer","order",
                       "own","paint","part","plan","plant","present","print",
                       "question","record","report","result","reward","rule",
                       "sign","signal","start","suit","support","test",
                       "trust","view","work"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::VERB, StemClass::REG));
        }

        // ══════════════════════════════════════════════════════════════
        // VERBS, Y_FINAL  (y -> ies for 3SG; y -> ied for PAST)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"amplify","apply","carry","certify","clarify","classify",
                       "comply","copy","cry","deny","deploy","diversify",
                       "dry","envy","fly","fry","glorify","identify","imply",
                       "justify","magnify","modify","multiply","notify",
                       "occupy","pity","qualify","rectify","rely","reply",
                       "satisfy","simplify","specify","study","supply",
                       "terrify","try","unify","vary","verify","worry"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::VERB, StemClass::Y_FINAL));
        }

        // ══════════════════════════════════════════════════════════════
        // VERBS, SIBILANT  (3SG +es: pushes, watches)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"address","assess","attach","bleach","brush","buzz",
                       "catch","clash","coach","crash","crush","detach",
                       "discuss","distinguish","establish","fix","flash",
                       "flush","focus","fuzz","hatch","launch","latch",
                       "match","mix","pass","patch","preach","process",
                       "push","quiz","reach","refresh","relax","rush",
                       "scratch","search","sketch","slash","switch",
                       "tax","toss","touch","watch","wish"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::VERB, StemClass::SIBILANT));
        }

        // ══════════════════════════════════════════════════════════════
        // VERBS, DOUBLE_CONS  (stop->stopped, hop->hopping)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"beg","clap","drop","drum","fan","fit","flag","grab",
                       "grip","hop","hug","jog","knit","log","map","nap",
                       "nod","pat","pin","pit","plan","pop","rip","rob",
                       "rub","run","set","sit","skip","slam","slap","slip",
                       "snap","stab","step","stop","stun","swim","tap",
                       "tip","top","trim","trip","trot","tug","wrap"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::VERB, StemClass::DOUBLE_CONS));
        }

        // ══════════════════════════════════════════════════════════════
        // VERBS, SILENT_E  (hope->hoping, make->making)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"achieve","argue","bake","blame","breathe","celebrate",
                       "change","chase","close","combine","come","complete",
                       "create","dance","decide","define","describe","desire",
                       "divide","drive","eliminate","embrace","encourage",
                       "escape","evaluate","examine","excite","execute",
                       "exercise","explore","force","guide","hate","hope",
                       "imagine","improve","include","increase","influence",
                       "inspire","introduce","invite","judge","love","make",
                       "manage","measure","move","name","notice","observe",
                       "overcome","participate","practice","produce","promise",
                       "prove","provide","raise","realize","receive","recognize",
                       "reduce","release","remove","require","resolve","ride",
                       "rise","save","schedule","secure","sense","serve",
                       "shake","share","smile","solve","state","store",
                       "structure","survive","take","taste","trace","trade",
                       "use","vote","write"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::VERB, StemClass::SILENT_E));
        }

        // ══════════════════════════════════════════════════════════════
        // VERBS, IRREGULAR
        // ══════════════════════════════════════════════════════════════

        // IRR_GO (suppletive: go -> went/gone)
        auto go = std::make_shared<Lexeme>("go", PartOfSpeech::VERB, StemClass::IRR_GO);
        go->add_irregular_form(Lexeme::Feature::PAST, "went");
        go->add_irregular_form(Lexeme::Feature::PAST_PART, "gone");
        lexicon.insert_lexeme(go);

        // IRR_BE
        auto be = std::make_shared<Lexeme>("be", PartOfSpeech::VERB, StemClass::IRR_BE);
        be->add_irregular_form(Lexeme::Feature::THIRD_SG, "is");
        be->add_irregular_form(Lexeme::Feature::PAST, "was");
        be->add_irregular_form(Lexeme::Feature::PAST_PART, "been");
        lexicon.insert_lexeme(be);

        // IRR_HAVE
        auto have = std::make_shared<Lexeme>("have", PartOfSpeech::VERB, StemClass::IRR_HAVE);
        have->add_irregular_form(Lexeme::Feature::THIRD_SG, "has");
        have->add_irregular_form(Lexeme::Feature::PAST, "had");
        have->add_irregular_form(Lexeme::Feature::PAST_PART, "had");
        lexicon.insert_lexeme(have);

        // IRR_DO
        auto do_ = std::make_shared<Lexeme>("do", PartOfSpeech::VERB, StemClass::IRR_DO);
        do_->add_irregular_form(Lexeme::Feature::THIRD_SG, "does");
        do_->add_irregular_form(Lexeme::Feature::PAST, "did");
        do_->add_irregular_form(Lexeme::Feature::PAST_PART, "done");
        lexicon.insert_lexeme(do_);

        // IRR_SING class (i/a/u ablaut)
        for (auto& p : std::vector<std::tuple<std::string,std::string,std::string>>{
                {"sing","sang","sung"}, {"ring","rang","rung"},
                {"spring","sprang","sprung"}, {"drink","drank","drunk"},
                {"shrink","shrank","shrunk"}, {"stink","stank","stunk"},
                {"begin","began","begun"}, {"swim","swam","swum"},
                {"win","won","won"}}) {
            auto lex = std::make_shared<Lexeme>(std::get<0>(p), PartOfSpeech::VERB, StemClass::IRR_SING);
            lex->add_irregular_form(Lexeme::Feature::PAST,      std::get<1>(p));
            lex->add_irregular_form(Lexeme::Feature::PAST_PART, std::get<2>(p));
            lexicon.insert_lexeme(lex);
        }

        // Strong verbs (use REG class for base; irregular forms stored)
        for (auto& p : std::vector<std::tuple<std::string,std::string,std::string>>{
                {"break","broke","broken"}, {"choose","chose","chosen"},
                {"drive","drove","driven"}, {"fall","fell","fallen"},
                {"freeze","froze","frozen"},
                {"give","gave","given"},    {"grow","grew","grown"},
                {"know","knew","known"},    {"ride","rode","ridden"},
                {"rise","rose","risen"},    {"see","saw","seen"},
                {"shake","shook","shaken"}, {"speak","spoke","spoken"},
                {"steal","stole","stolen"}, {"take","took","taken"},
                {"throw","threw","thrown"}, {"wear","wore","worn"},
                {"write","wrote","written"}, {"bite","bit","bitten"},
                {"hide","hid","hidden"},    {"wake","woke","woken"},
                {"bear","bore","born"},     {"swear","swore","sworn"},
                {"tear","tore","torn"},     {"forget","forgot","forgotten"},
                {"forgive","forgave","forgiven"}, {"forbid","forbade","forbidden"}}) {
            auto lex = std::make_shared<Lexeme>(std::get<0>(p), PartOfSpeech::VERB, StemClass::IRR_GO);
            lex->add_irregular_form(Lexeme::Feature::PAST,      std::get<1>(p));
            lex->add_irregular_form(Lexeme::Feature::PAST_PART, std::get<2>(p));
            lexicon.insert_lexeme(lex);
        }

        // t/d irregulars (same past and past-participle)
        for (auto& p : std::vector<std::pair<std::string,std::string>>{
                {"bring","brought"}, {"buy","bought"}, {"catch","caught"},
                {"fight","fought"}, {"seek","sought"}, {"teach","taught"},
                {"think","thought"}, {"feel","felt"}, {"keep","kept"},
                {"leave","left"}, {"mean","meant"}, {"send","sent"},
                {"spend","spent"}, {"build","built"}, {"bend","bent"},
                {"lend","lent"}, {"rend","rent"}, {"spend","spent"},
                {"bleed","bled"}, {"breed","bred"}, {"feed","fed"},
                {"flee","fled"}, {"hold","held"}, {"lead","led"},
                {"meet","met"}, {"read","read"}, {"shoot","shot"},
                {"stand","stood"}, {"understand","understood"},
                {"lose","lost"}, {"tell","told"},
                {"sell","sold"}, {"find","found"}, {"bind","bound"},
                {"grind","ground"}, {"wind","wound"}, {"hang","hung"},
                {"strike","struck"}, {"stick","stuck"}, {"dig","dug"},
                {"spin","spun"}, {"spit","spat"}, {"split","split"},
                {"spread","spread"}, {"shed","shed"}, {"hit","hit"},
                {"hurt","hurt"}, {"put","put"}, {"cut","cut"},
                {"set","set"}, {"let","let"}, {"cast","cast"},
                {"cost","cost"}, {"burst","burst"}, {"thrust","thrust"},
                {"bet","bet"}, {"bid","bid"}, {"knit","knit"},
                {"quit","quit"}, {"rid","rid"}, {"slit","slit"},
                {"shut","shut"}, {"upset","upset"}, {"wed","wed"},
                {"come","came"}, {"run","ran"}, {"get","got"},
                {"forget","forgot"}, {"become","became"}}) {
            auto lex = std::make_shared<Lexeme>(p.first, PartOfSpeech::VERB, StemClass::IRR_GO);
            lex->add_irregular_form(Lexeme::Feature::PAST, p.second);
            lex->add_irregular_form(Lexeme::Feature::PAST_PART, p.second);
            lexicon.insert_lexeme(lex);
        }

        // ══════════════════════════════════════════════════════════════
        // ADJECTIVES, REG
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"abstract","accurate","active","actual","acute",
                       "afraid","alert","alive","ancient","annual","apparent",
                       "awful","awkward","bad","beautiful","brief","bright",
                       "broad","calm","careful","central","certain","civil",
                       "clean","clear","clever","cold","complex","cool",
                       "correct","cruel","dark","dead","decent","deep",
                       "direct","distinct","dominant","dramatic","dull",
                       "dumb","eager","equal","evil","exact","external",
                       "fair","false","familiar","final","firm","flat",
                       "formal","frequent","full","general","gentle","good",
                       "grand","grateful","great","harsh","helpful","high",
                       "honest","hot","human","ideal","illegal","important",
                       "informal","innocent","internal","just","keen","kind",
                       "large","legal","light","logical","long","loud",
                       "loyal","mad","mental","moral","mutual","natural",
                       "neutral","normal","odd","open","perfect","plain",
                       "pleasant","political","poor","popular","practical",
                       "present","principal","proud","rational","real",
                       "recent","regular","relevant","rich","rough","round",
                       "rural","safe","sad","short","significant","silent",
                       "simple","small","smooth","social","special","spiritual",
                       "straight","strict","strong","sudden","superior",
                       "sweet","tall","technical","typical","universal",
                       "useful","vast","violent","vital","weak","wild",
                       "wonderful","young"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::ADJECTIVE, StemClass::REG));
        }

        // ══════════════════════════════════════════════════════════════
        // ADJECTIVES, Y_FINAL  (happy->happier/happiest)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"angry","busy","chilly","cloudy","cozy","crazy",
                       "creepy","dirty","dizzy","dusty","early","easy",
                       "empty","fancy","filthy","friendly","funny","fuzzy",
                       "greasy","greedy","grumpy","guilty","happy","healthy",
                       "heavy","hilly","hungry","icy","jolly","juicy",
                       "lazy","lively","lonely","lovely","lucky","messy",
                       "mighty","muddy","nasty","noisy","orderly","pretty",
                       "rocky","rowdy","rusty","shiny","silly","sleepy",
                       "sloppy","smelly","smoky","sneaky","soggy","spicy",
                       "spooky","stormy","stuffy","sweaty","tasty","tidy",
                       "tiny","ugly","uneasy","unhappy","weary","wealthy",
                       "windy","witty","worthy","wriggly"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::ADJECTIVE, StemClass::Y_FINAL));
        }

        // ══════════════════════════════════════════════════════════════
        // ADJECTIVES, DOUBLE_CONS  (big->bigger/biggest)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"big","dim","fat","fit","flat","grim","hot","mad",
                       "red","sad","slim","tan","thin","wet"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::ADJECTIVE, StemClass::DOUBLE_CONS));
        }

        // ══════════════════════════════════════════════════════════════
        // ADJECTIVES, SILENT_E  (nice->nicer/nicest)
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"brave","close","cute","fine","free","gentle","late",
                       "loose","nice","pale","pure","rare","rude","safe",
                       "sane","severe","simple","sincere","sole","strange",
                       "true","vague","wise","white","whole","wide","wise"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::ADJECTIVE, StemClass::SILENT_E));
        }

        // ══════════════════════════════════════════════════════════════
        // ADJECTIVES, IRREGULAR (good/bad/far)
        // ══════════════════════════════════════════════════════════════
        auto good = std::make_shared<Lexeme>("good", PartOfSpeech::ADJECTIVE, StemClass::IRR_GO);
        good->add_irregular_form(Lexeme::Feature::COMP, "better");
        good->add_irregular_form(Lexeme::Feature::SUP,  "best");
        lexicon.insert_lexeme(good);

        auto bad_adj = std::make_shared<Lexeme>("bad", PartOfSpeech::ADJECTIVE, StemClass::IRR_GO);
        bad_adj->add_irregular_form(Lexeme::Feature::COMP, "worse");
        bad_adj->add_irregular_form(Lexeme::Feature::SUP,  "worst");
        lexicon.insert_lexeme(bad_adj);

        auto far_adj = std::make_shared<Lexeme>("far", PartOfSpeech::ADJECTIVE, StemClass::IRR_GO);
        far_adj->add_irregular_form(Lexeme::Feature::COMP, "farther");
        far_adj->add_irregular_form(Lexeme::Feature::SUP,  "farthest");
        lexicon.insert_lexeme(far_adj);

        // ══════════════════════════════════════════════════════════════
        // ADVERBS, REG
        // ══════════════════════════════════════════════════════════════
        for (auto s : {"abroad","accordingly","actually","again","ahead",
                       "almost","already","also","although","always","anywhere",
                       "apparently","approximately","barely","basically",
                       "certainly","clearly","closely","consequently",
                       "currently","deeply","directly","down","effectively",
                       "entirely","equally","especially","essentially",
                       "eventually","exactly","extremely","finally","firmly",
                       "frequently","fully","generally","gradually","greatly",
                       "highly","honestly","immediately","increasingly",
                       "initially","instead","largely","later","merely",
                       "naturally","nearly","never","normally","now","often",
                       "only","possibly","presumably","primarily","probably",
                       "quickly","rapidly","rarely","really","recently",
                       "relatively","roughly","seldom","seriously","shortly",
                       "significantly","simply","slightly","slowly","sometimes",
                       "somewhere","soon","specifically","still","strongly",
                       "suddenly","supposedly","together","totally","truly",
                       "ultimately","usually","widely","yet"}) {
            lexicon.insert_lexeme(std::make_shared<Lexeme>(s, PartOfSpeech::ADVERB, StemClass::REG));
        }

        // Load supplementary entries from TSV (non-fatal if file missing)
        LexiconLoader::load_from_tsv("data/english_lexicon.tsv", lexicon);
    }

    static void load_rules(Analyzer& analyzer) {
        // Plural rules
        analyzer.add_rule(std::make_shared<RegularPluralRule>());
        analyzer.add_rule(std::make_shared<SibilantPluralRule>());
        analyzer.add_rule(std::make_shared<YPluralRule>());
        analyzer.add_rule(std::make_shared<IrregularPluralRule>());

        // Verb inflection rules
        analyzer.add_rule(std::make_shared<RegularPastRule>());
        analyzer.add_rule(std::make_shared<RegularPresPartRule>());
        analyzer.add_rule(std::make_shared<Regular3SGRule>());
        analyzer.add_rule(std::make_shared<Sibilant3SGRule>());
        analyzer.add_rule(std::make_shared<YFinal3SGRule>());
        analyzer.add_rule(std::make_shared<YFinalPastRule>());
        analyzer.add_rule(std::make_shared<DoubleConsPastRule>());
        analyzer.add_rule(std::make_shared<DoubleConsPresPartRule>());
        analyzer.add_rule(std::make_shared<SilentEPastRule>());
        analyzer.add_rule(std::make_shared<SilentEPresPartRule>());

        // Adjective degree rules
        analyzer.add_rule(std::make_shared<RegularCompRule>());
        analyzer.add_rule(std::make_shared<RegularSupRule>());
        analyzer.add_rule(std::make_shared<YFinalCompRule>());
        analyzer.add_rule(std::make_shared<YFinalSupRule>());

        // Derivational rules
        analyzer.add_derivation_rule(std::make_shared<NessRule>());
        analyzer.add_derivation_rule(std::make_shared<LyRule>());
        analyzer.add_derivation_rule(std::make_shared<AgentiveRule>());
        analyzer.add_derivation_rule(std::make_shared<TionRule>());
        analyzer.add_derivation_rule(std::make_shared<MentRule>());
    }

    static void initialize(SymbolTable& symbols, LexiconFSA& lexicon, Analyzer& analyzer) {
        load_lexicon(lexicon);
        load_rules(analyzer);
        analyzer.build_irregular_index();
    }
};

#endif // ENGLISH_CONFIG_H
