#pragma once

namespace gvdi {
class Polymorphic {
  public:
	virtual ~Polymorphic() = default;

	Polymorphic() = default;
	Polymorphic(Polymorphic const&) = delete;
	Polymorphic(Polymorphic&&) = delete;
	Polymorphic& operator=(Polymorphic const&) = delete;
	Polymorphic& operator=(Polymorphic&&) = delete;
};
} // namespace gvdi
