#ifndef PLAYER_H
#define PLAYER_H

#include <memory>


namespace utuputki {


class Config;
class Utuputki;


class Player {
	struct PlayerImpl;
	std::unique_ptr<PlayerImpl> impl;


	Player()                               = delete;

	Player(const Player &other)            = delete;
	Player &operator=(const Player &other) = delete;

	Player(Player &&other)                 = delete;
	Player &operator=(Player &&other)      = delete;

public:


	Player(Utuputki &utuputki, const Config &config);

	~Player();


	void run();

	void shutdown(bool immediate);

	void notifyMediaUpdate();

	void skipCurrent();
};


}  // namespace utuputki


#endif  // PLAYER_H
