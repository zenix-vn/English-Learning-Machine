"""The "Học Online" programme: a fixed path of lessons from first words to short conversations (CEFR A1 to A2).

The learner follows the lessons in order, one at a time. Sophia teaches the current lesson by voice; when the learner has
practised it she calls the finish_lesson tool and the next lesson becomes current. Progress is kept per device in
vocab.db (table lesson_progress, see tools.py).

Each lesson: id, title_vi, goal_vi (what the learner can do afterwards), words (vocabulary to teach, English), phrases
(model sentences, English) and role_play (a tiny scene for the practice step, English instruction for the model).
Keep lessons short: a lesson is about 10 minutes of speaking, 5 to 8 words and 2 to 4 sentences.
"""

UNITS = [
    ("Chào hỏi", [
        ("Chào và tạm biệt", "chào hỏi và tạm biệt bằng tiếng Anh",
         ["hello", "hi", "goodbye", "good morning", "good night", "see you", "please", "thank you"],
         ["Hello, how are you?", "I am fine, thank you.", "See you tomorrow."],
         "The learner meets Sophia in the morning, then says goodbye in the evening."),
        ("Giới thiệu bản thân", "nói tên, tuổi và nơi ở của mình",
         ["name", "age", "years old", "from", "Vietnam", "live", "nice to meet you"],
         ["My name is Lan.", "I am ten years old.", "I am from Vietnam."],
         "Two new classmates introduce themselves to each other."),
        ("Số đếm từ 1 đến 20", "đếm và nói số từ 1 đến 20",
         ["one", "two", "three", "five", "ten", "twelve", "fifteen", "twenty"],
         ["I have three books.", "How old are you?", "I am twelve."],
         "Counting game: Sophia shows how many things she has, the learner counts and answers."),
        ("Bảng chữ cái", "đọc bảng chữ cái và đánh vần tên mình",
         ["alphabet", "letter", "spell", "A B C", "name", "write", "again"],
         ["How do you spell your name?", "L, A, N.", "Can you say it again?"],
         "Sophia asks the learner to spell their name and a few easy words, letter by letter."),
    ]),
    ("Thế giới của em", [
        ("Gia đình", "nói về các thành viên trong gia đình",
         ["mother", "father", "sister", "brother", "grandmother", "grandfather", "family", "baby"],
         ["This is my mother.", "I have one sister.", "My father is tall."],
         "The learner shows a family photo and tells Sophia who is who."),
        ("Đồ vật trong lớp", "gọi tên đồ vật và hỏi đây là gì",
         ["book", "pen", "pencil", "bag", "desk", "chair", "ruler", "board"],
         ["What is this?", "It is a pen.", "That is my bag."],
         "Guess game: Sophia describes a classroom object, the learner names it and asks What is this?"),
        ("Màu sắc", "nói màu sắc của đồ vật",
         ["red", "blue", "green", "yellow", "black", "white", "pink", "orange"],
         ["What color is it?", "It is red.", "My bag is blue."],
         "Sophia names things in a room, the learner says what color each one is."),
        ("Con vật", "gọi tên con vật và nói em thích con vật nào",
         ["cat", "dog", "bird", "fish", "rabbit", "elephant", "monkey", "like"],
         ["I have a cat.", "I like dogs.", "The elephant is big."],
         "A trip to the zoo: the learner says which animals they see and which ones they like."),
    ]),
    ("Cuộc sống hằng ngày", [
        ("Đồ ăn và thức uống", "nói em thích và muốn ăn uống gì",
         ["rice", "bread", "egg", "milk", "water", "apple", "chicken", "hungry"],
         ["I like rice.", "I want some water.", "I am hungry."],
         "Breakfast table: Sophia offers food and drinks, the learner says what they like and want."),
        ("Giờ giấc và các ngày", "hỏi giờ và nói các ngày trong tuần",
         ["Monday", "Sunday", "today", "tomorrow", "morning", "afternoon", "o'clock", "week"],
         ["What time is it?", "It is seven o'clock.", "Today is Monday."],
         "Sophia asks what day it is and what time school starts."),
        ("Thói quen hằng ngày", "kể các việc em làm mỗi ngày",
         ["wake up", "eat breakfast", "go to school", "study", "play", "sleep", "every day", "always"],
         ["I wake up at six.", "I go to school every day.", "I always sleep at ten."],
         "Sophia interviews the learner about a normal day, from morning to night."),
        ("Thời tiết", "nói về thời tiết hôm nay",
         ["sunny", "rainy", "cloudy", "windy", "hot", "cold", "weather", "umbrella"],
         ["What is the weather like?", "It is sunny today.", "I need an umbrella."],
         "Weather report: the learner tells Sophia what the weather is and what to wear."),
    ]),
    ("Xung quanh em", [
        ("Cơ thể và cảm xúc", "nói em thấy thế nào",
         ["head", "hand", "eye", "happy", "sad", "tired", "sick", "feel"],
         ["How do you feel?", "I feel happy.", "My head hurts."],
         "Sophia asks how the learner feels and why, and answers kindly."),
        ("Địa điểm trong thành phố", "gọi tên nơi chốn và nói em đi đâu",
         ["school", "park", "shop", "hospital", "market", "bus stop", "library", "go to"],
         ["Where are you going?", "I am going to the park.", "The library is near here."],
         "Sophia asks where the learner goes on weekends and with whom."),
        ("Hỏi đường", "hỏi đường và chỉ đường đơn giản",
         ["left", "right", "straight", "turn", "near", "far", "next to", "excuse me"],
         ["Excuse me, where is the school?", "Turn left.", "Go straight."],
         "A tourist (Sophia) is lost, the learner gives directions to the park."),
        ("Mua sắm", "hỏi giá và mua đồ",
         ["how much", "expensive", "cheap", "dong", "buy", "money", "big", "small"],
         ["How much is it?", "It is fifty thousand dong.", "I want to buy this."],
         "Shop role play: Sophia is the shopkeeper, the learner buys two things."),
    ]),
    ("Nói về em", [
        ("Sở thích", "nói em thích và không thích làm gì",
         ["hobby", "music", "football", "read", "draw", "swim", "sing", "dance"],
         ["I like singing.", "I do not like swimming.", "My hobby is drawing."],
         "Sophia and the learner find three things they both like."),
        ("Em làm được gì", "nói em có thể và không thể làm gì",
         ["can", "cannot", "run", "jump", "cook", "ride a bike", "speak English", "fast"],
         ["I can swim.", "I cannot cook.", "Can you ride a bike?"],
         "Talent show: the learner tells what they can do, Sophia asks follow-up questions."),
        ("Chuyện hôm qua", "kể ngắn về việc đã làm hôm qua",
         ["yesterday", "was", "went", "ate", "watched", "played", "visited", "last week"],
         ["Yesterday I went to the park.", "I watched a movie.", "It was fun."],
         "Sophia asks what the learner did yesterday, one sentence at a time."),
        ("Kế hoạch ngày mai", "nói dự định sắp tới",
         ["going to", "tomorrow", "next week", "plan", "visit", "meet", "weekend", "holiday"],
         ["I am going to visit my grandmother.", "What are you going to do?", "I am going to play football."],
         "Weekend plans: the learner tells Sophia three things they are going to do."),
    ]),
    ("Giao tiếp thực tế", [
        ("Gọi món ở nhà hàng", "gọi món và hỏi tính tiền",
         ["menu", "order", "soup", "noodles", "juice", "bill", "delicious", "would like"],
         ["I would like noodles, please.", "Can I have the bill?", "It is delicious."],
         "Restaurant role play: Sophia is the waiter, the learner orders a meal and pays."),
        ("Khi bị ốm", "nói em đau ở đâu và xin giúp đỡ",
         ["doctor", "medicine", "fever", "cough", "headache", "stomach", "rest", "better"],
         ["I have a headache.", "I need to see a doctor.", "I feel better now."],
         "At the doctor: Sophia is the doctor, the learner describes how they feel."),
        ("Nhờ giúp đỡ và gọi điện", "nhờ giúp và nói chuyện điện thoại đơn giản",
         ["help", "phone", "call", "message", "sorry", "repeat", "slowly", "wait"],
         ["Can you help me, please?", "Could you speak slowly?", "Sorry, I do not understand."],
         "A short phone call: the learner asks Sophia for help and asks her to repeat."),
        ("Ôn tập: kể về một ngày của em", "kể một đoạn ngắn về ngày của mình",
         ["first", "then", "after that", "finally", "because", "very", "also", "favorite"],
         ["First I eat breakfast, then I go to school.", "My favorite subject is English.", "Finally I sleep."],
         "Final review: the learner tells a short story about their day, Sophia helps with connecting words."),
    ]),
]


def lessons() -> list[dict]:
    """Every lesson in teaching order."""
    out = []
    for u, (unit_title, items) in enumerate(UNITS, 1):
        for l, (title, goal, words, phrases, role_play) in enumerate(items, 1):
            out.append({"id": f"u{u}l{l}", "unit": u, "number": l, "unit_title": unit_title, "title": title,
                        "goal": goal, "words": words, "phrases": phrases, "role_play": role_play})
    return out


LESSONS = lessons()
BY_ID = {x["id"]: x for x in LESSONS}


def current(done: set[str]) -> dict | None:
    """The first lesson the learner has not finished, or None when the whole programme is done."""
    return next((x for x in LESSONS if x["id"] not in done), None)


def label(lesson: dict) -> str:
    """Spoken name, for example 'Chủ đề 1, bài 3: Số đếm từ 1 đến 20'."""
    return f"Chủ đề {lesson['unit']}, bài {lesson['number']}: {lesson['title']}"


def teaching_brief(lesson: dict, finished: int) -> str:
    """The instructions about the current lesson that are added to the tutor's system prompt."""
    return (
        f"CURRENT LESSON ({finished} of {len(LESSONS)} lessons finished): {label(lesson)}.\n"
        f"Goal: the learner can {lesson['goal']}.\n"
        f"Vocabulary: {', '.join(lesson['words'])}.\n"
        f"Key sentences: {' | '.join(lesson['phrases'])}.\n"
        f"Practice scene: {lesson['role_play']}\n"
        "How to teach it, one step at a time, waiting for the learner after every step:\n"
        "1. Warm up with one easy question. 2. Teach two or three words at a time: say the English word, its "
        "Vietnamese meaning, ask the learner to repeat. 3. Teach the key sentences the same way. 4. Practise with the "
        "practice scene, using the new words, correcting the single most important mistake kindly. 5. When the "
        "learner has used most of the words and sentences correctly, praise them and call finish_lesson. "
        "Do not call finish_lesson before the learner has practised. If the learner already knows the lesson well, "
        "move faster. If they want to stop, say a short goodbye; the lesson stays current for next time.")
