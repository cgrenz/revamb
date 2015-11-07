#ifndef _RANGE_H
#define _RANGE_H

#include <type_traits>
#include <vector>

template<typename Iterator>
class Range {
private:
  using reference = typename Iterator::reference;
  using difference_type = typename Iterator::difference_type;

public:
  Range(Iterator Begin, Iterator End) : Begin(Begin), End(End) { }
  template<typename ContainerT>
  Range(ContainerT&& Container) : Begin(Container.begin()),
                                  End(Container.end()) { }

  Iterator begin() const { return Begin; }
  Iterator end() const { return End; }

  std::vector<typename Iterator::value_type> toVector() const {
    std::vector<typename Iterator::value_type> Result;
    for (auto Element : *this)
      Result.push_back(Element);
    return Result;
  }

  reference operator[](const difference_type& n) const {
    return Begin[n];
  }

  difference_type size() const {
    return End - Begin;
  }

private:
  Iterator Begin;
  Iterator End;
};

template<typename ContainerT>
using RangeFromContainer = 
  Range<typename std::remove_reference<ContainerT>::type::iterator>;

template<typename ContainerT>
RangeFromContainer<ContainerT> make_range(ContainerT&& Container) {
  return RangeFromContainer<ContainerT>(Container);
}

template<typename T, typename OutputIterator>
void copy(Range<T> Source, OutputIterator Destination) {
  for (auto Element : Source)
    *Destination++ = Element;
}

#endif // _RANGE_H
