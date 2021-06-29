#pragma once
#include <seqan3/core/debug_stream.hpp>
#include <seqan3/io/sam_file/input.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>

#include <bamit/Record.hpp>

#include <numeric>

namespace bamit
{
/*! The IntervalNode class stores a single node which is a part of an interval tree. It stores a file position to the
 *  first read which intersects the median, along with pointers to its left and right children.
 *  Additionally, it stores the chromosome it is in, the start of the left-most record and the end of the right-most
 *  record.
 */
class IntervalNode
{
private:
    uint32_t start{}, end{};
    std::streampos file_position{-1};
    std::unique_ptr<IntervalNode> lNode{nullptr};
    std::unique_ptr<IntervalNode> rNode{nullptr};
public:
    /*!\name Constructors, destructor and assignment
     * \{
     */
    constexpr IntervalNode()                        = default; //!< Defaulted.
    IntervalNode(IntervalNode const &)              = default; //!< Defaulted.
    IntervalNode(IntervalNode &&)                   = default; //!< Defaulted.
    IntervalNode & operator=(IntervalNode const &)  = default; //!< Defaulted.
    IntervalNode & operator=(IntervalNode &&)       = default; //!< Defaulted.
    ~IntervalNode()                                 = default; //!< Defaulted.
     //!\}

     /*!
        \brief Get the left child node of current node.
        \return Returns a std::unique_ptr reference to the left child node.
     */
     std::unique_ptr<IntervalNode> & get_left_node()
     {
         return lNode;
     }

     /*!
        \brief Get the right child node of current node.
        \return Returns a std::unique_ptr reference to the right child node.
     */
     std::unique_ptr<IntervalNode> & get_right_node()
     {
         return rNode;
     }

     /*!
        \brief Get the start for the current node.
        \return Returns the start position of the left-most record which is stored in this node.
     */
     uint32_t const & get_start() const
     {
         return start;
     }

     /*!
        \brief Get the end for the current node.
        \return Returns the end position of the right-most record which is stored in this node.
     */
     uint32_t const & get_end() const
     {
         return end;
     }

     /*!
        \brief Get the file position to the first read stored by this node.
        \return Returns a reference to a std::streampos which can be used to seek to a file position.
     */
     std::streampos const & get_file_position() const
     {
         return file_position;
     }

     /*!
        \brief Set the file position to the first read for the current node.
        \param new_file_position The file position based on the records stored by this node.
     */
     void set_file_position(std::streampos new_file_position)
     {
         this->file_position = std::move(new_file_position);
     }

     /*!
        \brief Set the start value for the current node.
        \param s The start of the left-most record stored by this node.
     */
     void set_start(uint32_t s)
     {
         this->start = std::move(s);
     }

     /*!
        \brief Set the end value for the current node.
        \param e The end of the left-most record stored by this node.
     */
     void set_end(uint32_t e)
     {
         this->end = std::move(e);
     }

     /*!
        \brief Print the Interval Tree starting at this node.
        \param level The level of the current node.
     */
     void print(int32_t level)
     {
         std::string indent(level, '\t');
         seqan3::debug_stream << indent << "Level: " << level << '\n' <<
                                 indent << "Start, end: " << start << ", " << end << '\n' <<
                                 indent << "File position: " << this->get_file_position() << '\n';
         if (lNode)
         {
             seqan3::debug_stream << indent << "left node... \n";
             lNode->print(level + 1);
         }
         if (rNode)
         {
             seqan3::debug_stream << indent << "right node... \n";
             rNode->print(level + 1);
         }
     }

    template <class Archive>
    void serialize(Archive & ar)
    {
        ar(this->start, this->end, this->lNode, this->rNode, static_cast<std::streamoff>(this->file_position));
    }

};

/*!
   \brief Calculate the median for a set of records based on the starts and ends of all records.
   \param records_i The list of records to calculate a median off of.
   \return Returns a tuple of chromosome of median and the median value.

   The median is calculated by sorting all of the starts and ends from a list of records. Since each record
   has a start and end, the list is an even length and the median is the average of the middle two positions.
*/
uint32_t calculate_median(std::vector<Record> const & records_i)
{
    std::vector<uint32_t> values{};
    values.reserve(records_i.size() * 2);
    for (auto const & r : records_i)
    {
        values.push_back(r.start);
        values.push_back(r.end);
    }
    std::sort(values.begin(), values.end());

    return (values[values.size() / 2] + values[(values.size() - 1) / 2]) / 2;
}

/*!
   \brief Construct an interval tree given a set of records.
   \param node The current node to fill.
   \param records_i The list of records to create the tree over.
*/
void construct_tree(std::unique_ptr<IntervalNode> & node,
                    std::vector<Record> & records_i)
{
    // If there are no records, exit.
    if (records_i.empty())
    {
        return;
    }
    // Set node to an empty IntervalNode pointer.
    node = std::make_unique<IntervalNode>();

    // Calculate and set median.
    auto cur_median = calculate_median(records_i);

    // Get reads which intersect median.
    std::vector<Record> lRecords{};
    std::vector<Record> rRecords{};

    uint32_t start{0}, end{0};
    for (auto & r : records_i)
    {
        // Read ends before the median.
        if (r.end < cur_median)
        {
            // Push it back into the first empty vector.
            lRecords.push_back(std::move(r));
        }
        // Read starts after the median.
        else if (r.start > cur_median)
        {
            rRecords.push_back(std::move(r));
        }
        // Read intersects the median. Only store file position and start from the left-most read!
        // End is always updated while the read intersects the median.
        else
        {
            if (node->get_file_position() == -1)
            {
                node->set_file_position(r.file_position);
                start = r.start;
            }
            end = r.end;
        }
    }
    node->set_start(start);
    node->set_end(end);

    // Set left and right subtrees.
    construct_tree(node->get_left_node(), lRecords);
    construct_tree(node->get_right_node(), rRecords);
    return;
}

/*!
   \brief Entry point into the recursive tree construction.
   \param input_file The input file to construct the tree over.
   \param verbose Print verbose output.
   \return Returns a vector of IntervalNodes, each of which is the root node of an Interval Tree over its respective
           chromosome.
*/
std::vector<std::unique_ptr<IntervalNode>> index(sam_file_input_type & input_file, bool const & verbose = false)
{
    // First make sure alingment file is sorted by coordinate.
    if (input_file.header().sorting != "coordinate")
    {
        throw seqan3::format_error{"ERROR: Input file must be sorted by coordinate (e.g. samtools sort)"};
    }

    // Vector containing result, initialized to # of chromosomes in header.
    std::vector<std::unique_ptr<IntervalNode>> result;
    result.reserve(input_file.header().ref_ids().size());
    std::generate_n(std::back_inserter(result), input_file.header().ref_ids().size(),
                    [] { return std::make_unique<IntervalNode>(); });

    // List of records for a single chromosome.
    std::vector<Record> cur_records;

    uint32_t cur_index{0};
    for (auto it = input_file.begin(); it != input_file.end(); ++it)
    {
        if (unmapped(*it)) continue;
        uint32_t ref_id = (*it).reference_id().value();
        uint32_t position = (*it).reference_position().value();
        if (ref_id != cur_index)
        {
            if (verbose) seqan3::debug_stream << "Constructing tree for chromosome "
                                              << input_file.header().ref_ids()[cur_index] << "...";
            construct_tree(result[cur_index], cur_records);
            if (verbose) seqan3::debug_stream << " Done!\n";
            cur_records.clear();
            ++cur_index;
        }
        cur_records.emplace_back(position, position + get_length((*it).cigar_sequence()), it.file_position());
    }
    if (verbose) seqan3::debug_stream << "Constructing tree for chromosome "
                                      << input_file.header().ref_ids()[cur_index] << "...";
    construct_tree(result[cur_index], cur_records);
    if (verbose) seqan3::debug_stream << " Done!\n";

    return result;
}

/*!
   \brief Find the file offstream position that is close to the start of the range by traversing the tree. The offstream
          position is guaranteed to be to the left of the start.
   \param node The current node to search.
   \param start The start position of the search.
   \param end The end position of the search.
   \param file_position The resulting position position.
*/
void get_overlap_file_position(std::unique_ptr<IntervalNode> & node,
                             uint32_t const & start,
                             uint32_t const & end,
                             std::streampos & file_position)
{
    while(node)
    {
        uint32_t cur_start = node->get_start();
        uint32_t cur_end = node->get_end();

        /*
         * There are six possibilities:
         * 1. The query interval is wholly to the left of the current start (end < cur_start).
         * 2. The query interval is partially to the left of the current start (start < cur_start && end >= cur_start).
         * 3. The query interval is contained within the current start and end (start >= cur_start && end <= cur_end).
         * 4. The query interval contains the current start and end (start < cur_start && end > cur_end).
         * 5. The query interval is partially to the right of the current end (start <= cur_end && end > cur_end).
         * 6. The query interval is wholly to the right of the current end (start > cur_end).
         *
         * In case 1, do not store the file position and search the left subtree.
         * In cases 2 through 5, store the file position and search the left subtree.
         * In case 6, do not store the file position and search the right subtree.
        */
        if (end < cur_start)
        {
            node = std::move(node->get_left_node());
        }
        else if (start > cur_end)
        {
            node = std::move(node->get_right_node());
        }
        else
        {
            file_position = node->get_file_position();
            node = std::move(node->get_left_node());
        }
    }

    return;
}

/*!
   \brief Move the file position along until it points to the correct read.
   \param input The alignment file to use.
   \param start The start of the given query.
   \param file_position The position to move along.
 */
void get_correct_position(sam_file_input_type & input,
                        Position const & start,
                        std::streampos & file_position)
{
    auto it = input.begin();
    it.seek_to(file_position);
    for (; it != input.end(); ++it)
    {
        if (unmapped(*it)) continue;
        // If the read ends either at or after the start, it is the first read and its position should be returned.
        if (std::make_tuple((*it).reference_id().value(),
                            (*it).reference_position().value() + get_length((*it).cigar_sequence())) >= start)
        {
            file_position = it.file_position();
            return;
        }
    }
    // Don't think this is possible for the for loop to exit completely, but just in case...
    seqan3::debug_stream << "[ERROR] Improper file position/input file given.\n";
    file_position = -1;
}

/*!
   \brief Find the records which overlap a given start and end position.
   \param input The sam file input of type bamit::sam_file_input_type.
   \param node_list The list of interval trees.
   \param start The start position of the search.
   \param end The end position of the search.
   \param verbose Print verbose output.
   \param outname The output filename. If not provided the function will only return the file position and
                  not write to any file.

   \return Returns the file position of the first read in the interval, or -1 if no reads are found.
*/
std::streampos get_overlap_records(sam_file_input_type & input,
                                   std::vector<std::unique_ptr<IntervalNode>> & node_list,
                                   Position const & start,
                                   Position const & end,
                                   bool const & verbose = false,
                                   std::filesystem::path const & outname = "")
{
    std::streampos file_position{-1};

    // Look for the file position using the tree for the starting chromosome.
    if (std::get<0>(start) == std::get<0>(end)) // Searching in one chromosome.
    {
        get_overlap_file_position(node_list[std::get<0>(start)], std::get<1>(start), std::get<1>(end), file_position);
    }
    else // Searching across multiple chromosomes.
    {
        for (uint32_t i = std::get<0>(start); i < std::get<0>(end); ++i)
        {
            // Start at given start only for the first chromosome, otherwise start searching from 0.
            // For the first chromosome, we want the actual start position of the query. If no reads
            // in the chromosome are in our query, we want to search the tree of the next chromosome
            // starting from the beginning.
            uint32_t start_position = std::get<0>(start) == i ? std::get<1>(start) : 0;
            // End at given end if at the last chromosome, otherwise end at end of chromosome.
            uint32_t end_position = std::get<0>(end) == i ? std::get<1>(end) : std::numeric_limits<uint32_t>::max();
            get_overlap_file_position(node_list[i], start_position, end_position, file_position);

            // If we find the left-most position we can stop. Otherwise we have to check the next tree if
            // the overlap spans more than one chromosome.
            if (file_position != -1)
                break;
        }
    }

    if (file_position == -1)
    {
        if (verbose) seqan3::debug_stream << "No overlapping reads found.\n";
        return file_position;
    }

    get_correct_position(input, start, file_position);

    if (!outname.empty())
    {
        // Need to extract chromosome lengths for the output header file.
        std::vector<int32_t> ref_lengths{};
        std::transform(std::begin(input.header().ref_id_info), std::end(input.header().ref_id_info),
                       std::back_inserter(ref_lengths), [](auto const & pair){ return std::get<0>(pair); });
        seqan3::sam_file_output fout{outname, input.header().ref_ids(), ref_lengths, bamit::sam_file_output_fields{}};
        auto it = input.begin();
        it.seek_to(file_position);
        for (; it != input.end(); ++it)
        {
            if (unmapped(*it)) continue;
            if (std::make_tuple((*it).reference_id().value(), (*it).reference_position().value()) > end)
            {
                break;
            }
            fout.push_back(*it);
        }
    }
    return file_position;
}

template <class Archive>
void write(std::vector<std::unique_ptr<IntervalNode>> const & node_list, Archive & archive)
{
    archive(node_list);
}

template <class Archive>
void read(std::vector<std::unique_ptr<IntervalNode>> & node_list, Archive & archive)
{
    archive(node_list);
}
}
